/*
 * audiosync.c: Implements the synchronization steps
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

// #include "stream.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string>
#include <pthread.h>
#include <atomic>
#include <sys/socket.h>
#include <netdb.h>
#include <android/log.h>

#include "libmsntp/libmsntp.h"
#include "jrtplib/rtpsession.h"
#include "jrtplib/rtppacket.h"
#include "jrtplib/rtpsourcedata.h"
#include "jrtplib/rtpudpv4transmitter.h"
#include "jrtplib/rtpipv4address.h"
#include "jrtplib/rtpipv6address.h"
#include "jrtplib/rtpsessionparams.h"

#include "audiostream.h"
#include "decoder.h"
#include "audioplayer.h"

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioSync", __VA_ARGS__)

using namespace jrtplib;

class ASRTPSession : public jrtplib::RTPSession  {

protected:
    RTPAddress* addressFromData(RTPSourceData *dat) {
        short port = 0;
        const RTPAddress *addr;
        if (dat->GetRTPDataAddress() != 0) addr = dat->GetRTPDataAddress();
        else if (dat->GetRTCPDataAddress() != 0) {
            addr = dat->GetRTCPDataAddress();
            port = -1;
        }
        if (addr) {
            if(addr->GetAddressType() == RTPAddress::IPv4Address) {
                const RTPIPv4Address *v4addr = (const RTPIPv4Address *) (addr);
                return new RTPIPv4Address(v4addr->GetIP(), v4addr->GetPort()+port);
            } else if(addr->GetAddressType() == RTPAddress::IPv6Address) {
                const RTPIPv6Address *v6addr = (const RTPIPv6Address *) (addr);
                return new RTPIPv6Address(v6addr->GetIP(), v6addr->GetPort()+port);
            }
        }
        return NULL;
    }

    void OnNewSource(jrtplib::RTPSourceData * dat) {
        if (dat->IsOwnSSRC())
            return;

        debugLog("Added new source");
        RTPAddress *dest = addressFromData(dat);
        if (dest) {
            AddDestination(*dest);
            delete(dest);
        }
    }

    void OnBYEPacket(jrtplib::RTPSourceData * dat) {
        if (dat->IsOwnSSRC())
            return;

        debugLog("Received bye package");
        RTPAddress *dest = addressFromData(dat);
        if (dest != NULL) {
            DeleteDestination(*dest);
            delete(dest);
        }
    }
    void OnRemoveSource(jrtplib::RTPSourceData * dat) {
        if (dat->IsOwnSSRC())
            return;
        if (dat->ReceivedBYE())
            return;

        debugLog("Removing source");
        RTPAddress *dest = addressFromData(dat);
        if (dest != NULL) {
            DeleteDestination(*dest);
            delete(dest);
        }
    }
};

#define SNTP_PORT_OFFSET 5
/*#define RTP_PORT 32443
#define RTCP_PORT 32444*/

// Opaque pointer
// TODO make it private, use the init method to return an instance, alternatively just use a c++ class
struct audiostream_context {
    //long timeDiff[128];
    uint16_t portbase;
    char *serverhost;

    bool isRunning;
    pthread_t networkThread, ntpThread;

    //struct timeval clockOffset;
    AMediaExtractor *extractor;
    /*uint8_t *buffer;
    size_t bufferLength;*/
    /*
    // 16 bit per sample. Assume 44,1 kHz
    // Let's buffer for 3 seconds. Use this like a ringbuffer
#define FRAME_BUFFER_SIZE 44100 * 3
    uint16_t* frameBuffer;
    uint16_t samples[];*/
};

audiostream_context * audiostream_new() {
    return (audiostream_context *) malloc(sizeof(struct audiostream_context));
}
void audiostream_free(audiostream_context *ctx) {
    if (ctx->serverhost != NULL) free(ctx->serverhost);
    free(ctx);
}

void* _serveNTPServer(void *ctxPtr) {
    audiostream_context *ctx = (audiostream_context *)ctxPtr;

    uint16_t port = ctx->portbase + SNTP_PORT_OFFSET;
    if (msntp_start_server(port) != 0)
        return NULL;

    printf("Listening for SNTP clients on port %d...", port);
    while (ctx->isRunning) {
        int ret = msntp_serve();
        if (ret > 0 || ret < -1) return NULL;
    }

    msntp_stop_server();
    return NULL;
}

static void _checkerror(int rtperr) {
    if (rtperr < 0) {
        debugLog("RTP Error %s", RTPGetErrorString(rtperr).c_str());
        pthread_exit(0);
    }
}

// Waiting for connections and sending them data
void* _sendStream(void *ctxPtr) {
    audiostream_context *ctx = (audiostream_context *)ctxPtr;

    RTPSessionParams sessparams;
    // IMPORTANT: The local timestamp unit MUST be set, otherwise
    //            RTCP Sender Report info will be calculated wrong
    // In this case, we'll be sending 10 samples each second, so we'll
    // put the timestamp unit to (1.0/10.0)
    sessparams.SetOwnTimestampUnit(1.0/1000.0);// Simon: AMediaExtractor uses microseconds
    sessparams.SetReceiveMode(RTPTransmitter::ReceiveMode::AcceptAll);

    RTPUDPv4TransmissionParams transparams;
    transparams.SetPortbase(ctx->portbase);
    ASRTPSession sess;
    int status = sess.Create(sessparams, &transparams);
    _checkerror(status);
    sess.SetDefaultMark(false);

    debugLog("Started RTP server on port %u, now waiting for clients", ctx->portbase);

    bool waitaround = true;
    while (waitaround && ctx->isRunning) {
        sess.BeginDataAccess();
        if (sess.GotoFirstSourceWithData()) {
            do {
                RTPPacket *pack;
                while ((pack = sess.GetNextPacket()) != NULL) {
                    debugLog("Found our first sender !\n");
                    sess.DeletePacket(pack);
                    waitaround = false;
                }
            } while (sess.GotoNextSourceWithData());
        }
        sess.EndDataAccess();
        RTPTime::Wait(RTPTime(1, 0));// Wait 1s
        debugLog("Waiting....");
#ifndef RTP_SUPPORT_THREAD
        status = sess.Poll();
#endif
    }

    debugLog("Client connected, start sending");
    ssize_t written = 0;
    int64_t lastTime = 0;
    while(written >= 0 && ctx->isRunning) {

        int64_t time = 0;
        uint8_t buffer[1024];
        written = decoder_extractData(ctx->extractor, buffer, sizeof(buffer), &time);
        uint32_t timestampinc = (uint32_t) (time - lastTime);// Assuming it will fit
        lastTime = time;
        if (written < 0) {
            debugLog("Sender: End of stream");
            buffer[0] = '\0';
            status = sess.SendPacket(buffer, 1, 0, true, timestampinc);
        } else {
            debugLog("Sending timestamp %ld", (long)time);
            status = sess.SendPacket(buffer, (size_t)written, 0, false, timestampinc);
        }
        _checkerror(status);
        // Not really necessary
        sess.BeginDataAccess();
        // check incoming packets TODO use example4.cpp to add destinations
        if (sess.GotoFirstSourceWithData()) {
            do {
                RTPPacket *pack;
                while ((pack = sess.GetNextPacket()) != NULL) {
                    debugLog("The sender should not get packets !\n");
                    sess.DeletePacket(pack);
                }
            } while (sess.GotoNextSourceWithData());
        }
        sess.EndDataAccess();
// Without threads we have to do that for ourselves
#ifndef RTP_SUPPORT_THREAD
        status = sess.Poll();
        checkerror(status);
#endif // RTP_SUPPORT_THREAD

        RTPTime::Wait(RTPTime(1,0));// Wait 100ms
    }

    debugLog("I'm done sending");

    sess.BYEDestroy(RTPTime(2,0),0,0);
    return NULL;
}

// Packet format: [2 bytes, sequence number][]
/*void ssrc_cb(RtpSession *session) {
    debugLog("hey, the ssrc has changed !");
}*/

void* _receiveStream(void *ctxPtr) {
    audiostream_context *ctx = (audiostream_context *)ctxPtr;

    RTPSession sess;
    RTPUDPv4TransmissionParams transparams;
    RTPSessionParams sessparams;

    // IMPORTANT: The local timestamp unit MUST be set, otherwise
    //            RTCP Sender Report info will be calculated wrong
    // In this case, we'll be sending 10 samples each second, so we'll
    // put the timestamp unit to (1.0/10.0)
    sessparams.SetOwnTimestampUnit(1.0/10.0);

    sessparams.SetAcceptOwnPackets(false);
    sessparams.SetReceiveMode(RTPTransmitter::ReceiveMode::AcceptAll);
    //uint16_t portbase = RTP_PORT;
    transparams.SetPortbase(ctx->portbase);
    int status = sess.Create(sessparams, &transparams);
    _checkerror(status);

    RTPIPv4Address addr(ntohl(inet_addr(ctx->serverhost)), ctx->portbase);
    status = sess.AddDestination(addr);
    _checkerror(status);
    debugLog("Trying to receive data from %s:%u", ctx->serverhost, ctx->portbase);

    // TODO figure out how to check for connections
    size_t bufferLength = 1024*1024, bufferOffset = 0;
    uint8_t *pcmBuffer = (uint8_t *) malloc(bufferLength);

    AMediaFormat *format = AMediaFormat_new();
    // TODO figure this out
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mpeg");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, 44100);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);


    AMediaCodec *codec = AMediaCodec_createDecoderByType("audio/mpeg");
    status = AMediaCodec_configure(codec, format, NULL, NULL, 0);
    if(status != AMEDIA_OK) return NULL;

    debugLog("Sending Hi package");
    const char *hi = "HI";
    status = sess.SendPacket(hi, sizeof(hi), 0, false, sizeof(hi));// Say Hi, should cause the server to send data
    _checkerror(status);

    bool hasInput = true;
    bool hasOutput = true;
    int32_t first = -1;
    while(hasInput && ctx->isRunning) {

        sess.BeginDataAccess();
        // check incoming packets
        if (sess.GotoFirstSourceWithData()) {
            do {
                RTPPacket *pack;
                while ((pack = sess.GetNextPacket()) != NULL) {
                    uint8_t * payload = pack->GetPayloadData();
                    size_t length = pack->GetPayloadLength();
                    uint32_t timestamp = pack->GetTimestamp();

                    // record first timestamp and use differences
                    if (first == -1) first = timestamp;
                    timestamp -= first;
                    debugLog("Received package with timestamp %u", timestamp);

                    hasInput = !pack->HasMarker();// We repurposed this as end of stream
                    if (hasInput && length > 0 && timestamp > 0) {
                        status = decoder_enqueueBuffer(codec, payload, length, (int64_t)timestamp);
                        if (status != AMEDIA_OK) hasInput = false;
                    } else if (timestamp > 0) {
                        debugLog("Receiver: End of stream");
                        // Tell the codec we are done
                        decoder_enqueueBuffer(codec, NULL, -1, (int64_t)timestamp);
                    }
                    hasOutput = decoder_dequeueBuffer(codec, &format, &pcmBuffer, &bufferLength, &bufferOffset);

                    sess.DeletePacket(pack);
                }
            } while (sess.GotoNextSourceWithData());
        }

        sess.EndDataAccess();

#ifndef RTP_SUPPORT_THREAD
        status = sess.Poll();
        checkerror(status);
#endif // RTP_SUPPORT_THREAD
    }

    sess.BYEDestroy(RTPTime(1,0),0,0);

    debugLog("Received all data, finishing decoding");
    while(hasOutput && status == AMEDIA_OK && ctx->isRunning) {
        hasOutput = decoder_dequeueBuffer(codec, &format, &pcmBuffer, &bufferLength, &bufferOffset);
    }
    debugLog("Finished decoding, starting playing");
    // TODO don't use constants
    if (bufferOffset > 0) audioplayer_startPlayback(pcmBuffer, bufferOffset, 44100, 2);

    return NULL;
}

void audiostream_startStreaming(audiostream_context* ctx, uint16_t portbase, AMediaExtractor *extractor) {
    ctx->isRunning = true;
    ctx->portbase = portbase;
    ctx->extractor = extractor;
    //pthread_create(&(ctx->ntpThread), NULL, _serveNTPServer, ctx);
    pthread_create(&(ctx->networkThread), NULL, _sendStream, ctx);
}

void audiostream_startReceiving(audiostream_context*ctx, const char *host, uint16_t portbase) {
    ctx->isRunning = true;
    ctx->serverhost = strdup(host);
    ctx->portbase = portbase;

    // TODO try to set higher priorities on threads
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&(ctx->networkThread), &attr, _receiveStream, ctx);
}

void audiostream_stop(audiostream_context *ctx) {
    if (ctx->isRunning) {
        ctx->isRunning = false;// Should kill them all

        if(ctx->networkThread && pthread_join(ctx->networkThread, NULL)) {
            debugLog("Error joining thread");
        }
        ctx->networkThread = 0;
        if(ctx->ntpThread && pthread_join(ctx->ntpThread, NULL)) {
            debugLog("Error joining thread");
        }
        ctx->ntpThread = 0;
    }
}

