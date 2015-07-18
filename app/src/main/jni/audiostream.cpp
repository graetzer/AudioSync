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
#include <android/log.h>

#include "libmsntp/libmsntp.h"
#include "jrtplib/rtpsession.h"
#include "jrtplib/rtppacket.h"
#include "jrtplib/rtpsourcedata.h"
#include "jrtplib/rtpudpv4transmitter.h"
#include "jrtplib/rtpipv4address.h"
#include "jrtplib/rtpipv6address.h"
#include "jrtplib/rtpsessionparams.h"
#include "jrtplib/rtcpapppacket.h"

#include "audiostream.h"
#include "audioplayer.h"
#include "apppacket.h"
#include "decoder.h"

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioSync", __VA_ARGS__)
#define SNTP_PORT_OFFSET 3

using namespace jrtplib;

class SenderRTPSession : public jrtplib::RTPSession  {

protected:
    RTPAddress* addressFromData(RTPSourceData *dat) {
        short port = 0;
        const RTPAddress *addr = NULL;
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
        if (dat->IsOwnSSRC()) return;

        debugLog("Added new source");
        RTPAddress *dest = addressFromData(dat);
        if (dest) {
            AddDestination(*dest);
            delete(dest);

            // TODO in zukunft die toString methode von AMediaFormat nehmen
            // sollte man dekodieren können

            // TODO vielleicht doch besser per broadcast verbreiten.
            // Alternativ
            /*const char *codec = "audio/mpeg";
            size_t appdatalen = sizeof(audiostream_packet_format);
            audiostream_packet_format packet = {.samplesPerSec = 44100, .numChannels = 2};
            memcpy(&(packet.mime), codec, sizeof(codec));
            SendRTCPAPPPacket(AUDIOSTREAM_PACKET_APP_MEDIAFORMAT, audiostream_app_name, (uint8_t*)&packet, appdatalen);*/
        }
    }

    void OnBYEPacket(jrtplib::RTPSourceData * dat) {
        if (dat->IsOwnSSRC()) return;

        debugLog("Received bye package");
        RTPAddress *dest = addressFromData(dat);
        if (dest != NULL) {
            DeleteDestination(*dest);
            delete(dest);
        }
    }
    void OnRemoveSource(jrtplib::RTPSourceData * dat) {
        if (dat->IsOwnSSRC()) return;
        if (dat->ReceivedBYE()) return;

        debugLog("Removing source");
        RTPAddress *dest = addressFromData(dat);
        if (dest != NULL) {
            DeleteDestination(*dest);
            delete(dest);
        }
    }

    void OnAPPPacket(RTCPAPPPacket *apppacket,const RTPTime &receivetime,
                    const RTPAddress *senderaddress)					{
        if (apppacket->GetSubType() == AUDIOSTREAM_PACKET_APP_CLOCK
            && apppacket->GetAPPDataLength() >= sizeof(audiostream_packet_clock)) {
            audiostream_packet_clock *clock = (audiostream_packet_clock*) apppacket->GetAPPData();
            // TODO
        }
    }
};

class ReceiverRTPSession : public jrtplib::RTPSession {

protected:
    void OnAPPPacket(RTCPAPPPacket *apppacket,const RTPTime &receivetime,
                     const RTPAddress *senderaddress)					{
        // TODO
        if (apppacket->GetSubType() == AUDIOSTREAM_PACKET_APP_MEDIAFORMAT) {
            /*audiostream_packet_format *format = (audiostream_packet_format*) apppacket->GetAPPData();
            // TODO
            debugLog("Received media format: Samples(%d), Channels(%d), MIME(%s)",
                     format->samplesPerSec, format->numChannels, format->mime);*/
        }
    }
};

struct audiostream_context {
    //long timeDiff[128];
    uint16_t portbase;
    char *serverhost;

    bool isRunning;
    pthread_t networkThread, ntpThread;

    //struct timeval clockOffset;
    AMediaExtractor *extractor;// At some future point abstract the source behind an interface
    /*uint8_t *buffer;
    size_t bufferLength;*/
    /*
    // 16 bit per sample. Assume 44,1 kHz
    // Let's buffer for 3 seconds. Use this like a ringbuffer
#define FRAME_BUFFER_SIZE 44100 * 3
    uint16_t* frameBuffer;
    uint16_t samples[];*/
};

static void _checkerror(int rtperr) {
    if (rtperr < 0) {
        debugLog("RTP Error %s", RTPGetErrorString(rtperr).c_str());
        pthread_exit(0);
    }
}

// Waiting for connections and sending them data
void* _sendStream(void *ctxPtr) {
    audiostream_context *ctx = (audiostream_context *)ctxPtr;
    if (!ctx->extractor) {
        debugLog("No datasource");
        return NULL;
    }

    RTPSessionParams sessparams;
    // IMPORTANT: The local timestamp unit MUST be set, otherwise
    //            RTCP Sender Report info will be calculated wrong
    // In this case, we'll be sending 10 samples each second, so we'll
    // put the timestamp unit to (1.0/10.0)
    sessparams.SetOwnTimestampUnit(1.0/1000);//AMediaExtractor uses microseconds
    sessparams.SetReceiveMode(RTPTransmitter::ReceiveMode::AcceptAll);

    RTPUDPv4TransmissionParams transparams;
    transparams.SetPortbase(ctx->portbase);
    SenderRTPSession sess;
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
                    if(pack->GetPayloadLength() > 0
                       && pack->GetPayloadData()[pack->GetPayloadLength()-1] == '\0') {
                        debugLog("He said: %s", pack->GetPayloadData());
                    }
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
        _checkerror(status);
#endif
    }

    debugLog("Client connected, start sending");
    ssize_t written = 0;
    int64_t lastTime = -1;
    while(written >= 0 && ctx->isRunning) {

        int64_t time = 0;
        uint8_t buffer[1024];
        written = decoder_extractData(ctx->extractor, buffer, sizeof(buffer), &time);
        if (lastTime == -1) lastTime = time;
        uint32_t timestampinc = (uint32_t) (time - lastTime);// Assuming it will fit
        lastTime = time;
        if (written < 0) {
            buffer[0] = '\0';
            status = sess.SendPacket(buffer, 1, 0, true, timestampinc);
            debugLog("Sender: End of stream");
        } else {
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
        RTPTime::Wait(RTPTime(0, 25));// Wait 100ms
        //RTPTime::Wait(RTPTime(0,100));// Wait 100ms
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
    sessparams.SetOwnTimestampUnit(1.0/1000.0);

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

    AMediaFormat *format = AMediaFormat_new();
    // TODO figure this out
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mpeg");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, 44100);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
    AMediaFormat_setInt32(format, "encoder-delay", 576);
    AMediaFormat_setInt32(format, "encoder-padding", 1579);

    AMediaCodec *codec = AMediaCodec_createDecoderByType("audio/mpeg");
    status = AMediaCodec_configure(codec, format, NULL, NULL, 0);
    if(status != AMEDIA_OK) return NULL;
    status = AMediaCodec_start(codec);
    if(status != AMEDIA_OK) return NULL;

    debugLog("Sending Hi package");
    const char *hi = "HI";
    status = sess.SendPacket(hi, sizeof(hi), 0, false, sizeof(hi));// Say Hi, should cause the server to send data
    _checkerror(status);

    size_t pcmLength = 1024*1024, pcmOffset = 0;
    uint8_t *pcmBuffer = (uint8_t *) malloc(pcmLength);

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
                    if (hasInput && length > 0) {
                        status = decoder_enqueueBuffer(codec, payload, length, (int64_t)timestamp);
                        if (status != AMEDIA_OK) hasInput = false;
                    } else if (timestamp > 0) {
                        debugLog("Receiver: End of stream");
                        // Tell the codec we are done
                        decoder_enqueueBuffer(codec, NULL, -1, (int64_t)timestamp);
                    }
                    debugLog("Dequeuing");
                    hasOutput = decoder_dequeueBuffer(codec, &format, &pcmBuffer, &pcmLength, &pcmOffset);

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
        hasOutput = decoder_dequeueBuffer(codec, &format, &pcmBuffer, &pcmLength, &pcmOffset);
    }
    debugLog("Finished decoding, starting playing");
    // TODO don't use constants
    if (pcmOffset > 0) audioplayer_startPlayback(pcmBuffer, pcmOffset, 44100, 2);

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    return NULL;
}

audiostream_context * audiostream_new() {
    void *ptr = malloc(sizeof(audiostream_context));
    memset(ptr, 0, sizeof(audiostream_context));
    return (audiostream_context *)ptr;
}

void audiostream_free(audiostream_context *ctx) {
    if (ctx != NULL) {
        if (ctx->serverhost != NULL) free(ctx->serverhost);
        if (ctx->extractor != NULL) AMediaExtractor_delete(ctx->extractor);
        free(ctx);
    }
}

void* _serveNTPServer(void *ctxPtr) {
    audiostream_context *ctx = (audiostream_context *)ctxPtr;

    uint16_t port = ctx->portbase + (uint16_t)SNTP_PORT_OFFSET;
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

