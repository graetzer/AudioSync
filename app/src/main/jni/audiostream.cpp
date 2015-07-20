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
#include <atomic>
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
// IMPORTANT: The local timestamp unit MUST be set, otherwise
//            RTCP Sender Report info will be calculated wrong
//            In this case, we'll be sending 1000 samples each second, so we'll
//            put the timestamp unit to (1.0/1000.0)
// AMediaExtractor uses microseconds too, so we can transport the correct playback time
#define TIMESTAMP_UNITS (1.0 / 1000.0)

using namespace jrtplib;

static void _checkerror(int rtperr) {
    if (rtperr < 0) {
        debugLog("RTP Error: %s", RTPGetErrorString(rtperr).c_str());
        pthread_exit(0);
    }
}

void AudioStreamSession::log(const char *logStr, ...) {
    va_list ap;
    va_start(ap, logStr);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_DEBUG, "AudioStreamSession", logStr, ap);
#else
    vprintf(logStr, ap);
#endif
    va_end(ap);
}

class SenderSession : public AudioStreamSession {
public:
    std::atomic_int connectedSources;
    AMediaExtractor *extractor;

    ~SenderSession() {
        if (extractor) AMediaExtractor_delete(extractor);
    }

    void RunNetwork() {
        // Waiting for connections and sending them data
        if (!extractor) {
            log("No datasource");
            return;
        }
        if (format == NULL) {
            int idx = AMediaExtractor_getSampleTrackIndex(extractor);// If this returns -1, we crash
            format = AMediaExtractor_getTrackFormat(extractor, (size_t)idx);
        }

        int status = 0;
        while (connectedSources == 0 && isRunning) {
            RTPTime::Wait(RTPTime(2, 0));// Wait 2s
            log("Waiting for clients....");
#ifndef RTP_SUPPORT_THREAD
            status = sess->Poll();
            _checkerror(status);
#endif // RTP_SUPPORT_THREAD
        }
        if (!isRunning) return;

        log("Client connected, start sending");
        ssize_t written = 0;
        int64_t lastTime = -1;
        while (written >= 0 && isRunning) {
            int64_t time = 0;
            uint8_t buffer[1024];
            written = decoder_extractData(extractor, buffer, sizeof(buffer), &time);
            if (lastTime == -1) lastTime = time;
            uint32_t timestampinc = (uint32_t) (time - lastTime);// Assuming it will fit
            lastTime = time;

            if (written < 0) {
                buffer[0] = '\0';
                status = SendPacket(buffer, 1, 0, true, timestampinc);
                log("Sender: End of stream");
            } else {
                status = SendPacket(buffer, (size_t) written, 0, false, timestampinc);
                //log("Sending package for %.2fs", time / 1000.0);
            }
            _checkerror(status);

            // Don't decrease the waiting time too much, it seems sending a great number
            // of packets very fast, will cause the network (or the client) to drop a high
            // number of these packets. In any case the client needs to mitigate this.
            RTPTime::Wait(RTPTime(0, 1000));// Wait 1000us


            // Not really necessary, we are not using this
            BeginDataAccess();
            // check incoming packets TODO use example4.cpp to add destinations
            if (GotoFirstSourceWithData()) {
                do {
                    RTPPacket *pack;
                    while ((pack = GetNextPacket()) != NULL) {
                        log("The sender should not get packets !\n");
                        DeletePacket(pack);
                    }
                } while (GotoNextSourceWithData());
            }
            EndDataAccess();
// Without threads we have to do that for ourselves
#ifndef RTP_SUPPORT_THREAD
            status = Poll();
            _checkerror(status);
#endif // RTP_SUPPORT_THREAD
        }

        log("I'm done sending");
        BYEDestroy(RTPTime(2, 0), 0, 0);
    }

protected:
    RTPAddress *addressFromData(RTPSourceData *dat) {
        short port = 0;
        const RTPAddress *addr = NULL;
        if (dat->GetRTPDataAddress() != 0) addr = dat->GetRTPDataAddress();
        else if (dat->GetRTCPDataAddress() != 0) {
            addr = dat->GetRTCPDataAddress();
            port = -1;
        }
        if (addr) {
            if (addr->GetAddressType() == RTPAddress::IPv4Address) {
                const RTPIPv4Address *v4addr = (const RTPIPv4Address *) (addr);
                return new RTPIPv4Address(v4addr->GetIP(), v4addr->GetPort() + port);
            } else if (addr->GetAddressType() == RTPAddress::IPv6Address) {
                const RTPIPv6Address *v6addr = (const RTPIPv6Address *) (addr);
                return new RTPIPv6Address(v6addr->GetIP(), v6addr->GetPort() + port);
            }
        }
        return NULL;
    }

    void OnNewSource(jrtplib::RTPSourceData *dat) {
        if (dat->IsOwnSSRC()) return;

        log("Added new source");
        RTPAddress *dest = addressFromData(dat);
        if (dest) {
            AddDestination(*dest);
            delete(dest);
            connectedSources++;

            if (this->format) {
                const char *formatString = AMediaFormat_toString(format);
                SendRTCPAPPPacket(AUDIOSTREAM_PACKET_MEDIAFORMAT, audiostream_app_name,
                                  (uint8_t *) formatString, strlen(formatString));
            }
        }
    }

    void OnBYEPacket(jrtplib::RTPSourceData *dat) {
        if (dat->IsOwnSSRC()) return;

        log("Received bye package");
        RTPAddress *dest = addressFromData(dat);
        if (dest != NULL) {
            DeleteDestination(*dest);
            delete(dest);
            connectedSources--;
        }
    }

    void OnRemoveSource(jrtplib::RTPSourceData *dat) {
        if (dat->IsOwnSSRC()) return;
        if (dat->ReceivedBYE()) return;

        log("Removing source");
        RTPAddress *dest = addressFromData(dat);
        if (dest != NULL) {
            DeleteDestination(*dest);
            delete(dest);
            connectedSources--;
        }
    }

    void OnAPPPacket(RTCPAPPPacket *apppacket, const RTPTime &receivetime,
                     const RTPAddress *senderaddress) {
        if (apppacket->GetSubType() == AUDIOSTREAM_PACKET_CLOCKOFFSET
            && apppacket->GetAPPDataLength() >= sizeof(audiostream_clockOffset)) {
            audiostream_clockOffset *clock = (audiostream_clockOffset *) apppacket->GetAPPData();
            // TODO
        }
    }
};

static void *_runSendThread(void *ptr) {
    ((SenderSession *) ptr)->RunNetwork();
    return NULL;
}

static uint16_t _portbase;

void *_serveNTPServer(void *ctxPtr) {
    SenderSession *sess = (SenderSession *) ctxPtr;
    uint16_t port = _portbase + (uint16_t) SNTP_PORT_OFFSET;
    if (msntp_start_server(port) != 0)
        return NULL;

    printf("Listening for SNTP clients on port %d...", port);
    while (sess->IsRunning()) {
        int ret = msntp_serve();
        if (ret > 0 || ret < -1) return NULL;
    }

    msntp_stop_server();
    return NULL;
}

AudioStreamSession *audiostream_startStreaming(uint16_t portbase,
                                               AMediaExtractor *extractor) {
    SenderSession *sess = new SenderSession();
    RTPSessionParams sessparams;
    RTPUDPv4TransmissionParams transparams;
    // IMPORTANT: The local timestamp unit MUST be set, otherwise
    //            RTCP Sender Report info will be calculated wrong
    // In this case, we'll be sending 10 samples each second, so we'll
    // put the timestamp unit to (1.0/10.0)
    sessparams.SetOwnTimestampUnit(TIMESTAMP_UNITS);
    sessparams.SetReceiveMode(RTPTransmitter::ReceiveMode::AcceptAll);
    transparams.SetPortbase(portbase);
    int status = sess->Create(sessparams, &transparams);
    _checkerror(status);
    sess->SetDefaultMark(false);
    sess->extractor = extractor;
    pthread_create(&(sess->networkThread), NULL, &(_runSendThread), sess);
    debugLog("Started RTP server on port %u, now waiting for clients", portbase);

    _portbase = portbase;
    //pthread_create(&(ctx->ntpThread), NULL, _serveNTPServer, ctx);
    return sess;
}

class ReceiverSession : public AudioStreamSession {
public:
    ~ReceiverSession() {
        if (codec) AMediaCodec_delete(codec);
    }

    void RunNetwork() {
        log("Sending Hi package");
        const char *hi = "HI";
        int status = SendPacket(hi, sizeof(hi), 0, false,
                                sizeof(hi));// Say Hi, should cause the server to send data
        _checkerror(status);

        while (codec == NULL && isRunning) {
            log("Waiting for codec RTCP package...");
#ifndef RTP_SUPPORT_THREAD
            status = sess->Poll();
            _checkerror(status);
#endif // RTP_SUPPORT_THREAD
            RTPTime::Wait(RTPTime(2, 0));// Wait 1s
        }
        if (codec == NULL) {
            log("No codec set");
            return;
        }

        size_t pcmLength = 1024 * 1024, pcmOffset = 0;
        uint8_t *pcmBuffer = (uint8_t *) malloc(pcmLength);

        // Start decoder
        status = AMediaCodec_start(codec);
        if (status != AMEDIA_OK) return;

        bool hasInput = true;
        bool hasOutput = true;
        int32_t first = -1;
        int16_t sequence = 0;
        while (hasInput && isRunning) {

            BeginDataAccess();
            // check incoming packets
            if (GotoFirstSourceWithData()) {
                do {
                    RTPPacket *pack;
                    while ((pack = GetNextPacket()) != NULL) {

                        // Calculate playback time,
                        uint32_t timestamp = pack->GetTimestamp();
                        // record first timestamp and use differences
                        if (first == -1) {
                            first = timestamp;
                            sequence = pack->GetSequenceNumber();
                        }
                        timestamp -= first;
                        if (pack->GetSequenceNumber() != sequence+1) {
                            log("Sequence number jumped from %u to %u - %.2fs. Flushing codec", sequence, pack->GetSequenceNumber(), timestamp / 1000.0);
                        }
                        sequence = pack->GetSequenceNumber();

                        hasInput = !pack->HasMarker();// We repurposed this as end of file
                        if (hasInput) {
                            uint8_t *payload = pack->GetPayloadData();
                            size_t length = pack->GetPayloadLength();
                            status = decoder_enqueueBuffer(codec, payload, length, (int64_t) timestamp);
                            if (status != AMEDIA_OK) hasInput = false;
                        } else if (timestamp > 0) {
                            log("Receiver: End of file");
                            // Tell the codec we are done
                            decoder_enqueueBuffer(codec, NULL, -1, (int64_t) timestamp);
                        }
                        hasOutput = decoder_dequeueBuffer(codec, &pcmBuffer, &pcmLength,  &pcmOffset);
                        DeletePacket(pack);
                    }
                } while (GotoNextSourceWithData());
            }

            EndDataAccess();

#ifndef RTP_SUPPORT_THREAD
            status = sess.Poll();
            checkerror(status);
#endif // RTP_SUPPORT_THREAD
        }

        BYEDestroy(RTPTime(1, 0), 0, 0);

        log("Received all data");
        while (hasOutput && status == AMEDIA_OK && isRunning) {
            hasOutput = decoder_dequeueBuffer(codec, &pcmBuffer, &pcmLength, &pcmOffset);
        }
        AMediaCodec_stop(codec);
        log("Finished decoding, starting playing");

        if (pcmOffset > 0) {
            int32_t samples = 44100, channels = 1;
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &samples);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
            audioplayer_startPlayback(pcmBuffer, pcmOffset, samples, channels);
        }
    }

    void SetFormat(AMediaFormat *newFormat) {

        const char *mime;
        if (AMediaFormat_getString(newFormat, AMEDIAFORMAT_KEY_MIME, &mime)) {
            log("New format %s and creating codec", mime);

            AMediaCodec *newCodec = AMediaCodec_createDecoderByType(mime);
            if (newCodec) {
                int status = AMediaCodec_configure(newCodec, newFormat, NULL, NULL, 0);
                if (status != AMEDIA_OK) return;

                if (this->format != NULL) AMediaFormat_delete(this->format);
                this->format = newFormat;
                this->codec = newCodec;
            } else {
                log("Could not create codec");
            }

            // TODO in case a codec changes mid-stream, we would have to figure out if it
            // needs to be started
        }
    }

    void SetClockOffset(struct timeval val) {
        audiostream_clockOffset off = {.offsetSeconds = val.tv_sec, .offetUSeconds = val.tv_usec};
        this->SendRTCPAPPPacket(AUDIOSTREAM_PACKET_CLOCKOFFSET, audiostream_app_name, &off,
                                sizeof(audiostream_clockOffset));
    }

protected:

    AMediaCodec *codec;

    void OnAPPPacket(RTCPAPPPacket *apppacket, const RTPTime &receivetime,
                     const RTPAddress *senderaddress) {
        if (apppacket->GetSubType() == AUDIOSTREAM_PACKET_MEDIAFORMAT) {
            char *formatString = (char *) apppacket->GetAPPData();
            if (formatString[apppacket->GetAPPDataLength() - 1] == '\0') {
                log("Received format string %s", formatString);
                AMediaFormat *newFormat = audiostream_createFormat(formatString);

                if (newFormat != NULL) {
                    log("Parsed format string %s", AMediaFormat_toString(newFormat));
                    AMediaFormat_delete(newFormat);
                    // TODO activate this again
                    //SetFormat(newFormat);
                }
            }
        }
    }
};

static void *_runReceiveThread(void *ptr) {
    ((ReceiverSession *) ptr)->RunNetwork();
    return NULL;
}

static void *_runNTPClient(void *ptr) {
    ReceiverSession *sess = (ReceiverSession *) ptr;
    char *host = strdup("");
    int port = 123 + SNTP_PORT_OFFSET;

    while (sess->IsRunning()) {
        struct timeval tv;
        int err = msntp_get_offset(host, port, &tv);
        if (err) {
            debugLog("NTP client error %d", err);
        } else {
            sess->SetClockOffset(tv);
        }
    }
    free(host);
    return NULL;
}

AudioStreamSession *audiostream_startReceiving(const char *host, uint16_t portbase) {
    ReceiverSession *sess = new ReceiverSession();
    RTPUDPv4TransmissionParams transparams;
    RTPSessionParams sessparams;

    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mpeg");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, 44100);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
    AMediaFormat_setInt32(format, "encoder-delay", 576);
    AMediaFormat_setInt32(format, "encoder-padding", 1579);
    sess->SetFormat(format);

    sessparams.SetOwnTimestampUnit(TIMESTAMP_UNITS);
    sessparams.SetAcceptOwnPackets(false);
    sessparams.SetReceiveMode(RTPTransmitter::ReceiveMode::AcceptAll);
    //uint16_t portbase = RTP_PORT;
    transparams.SetPortbase(portbase);
    int status = sess->Create(sessparams, &transparams);
    _checkerror(status);

    debugLog("Adding Destination %s:%u", host, portbase);
    RTPIPv4Address addr(ntohl(inet_addr(host)), portbase);
    status = sess->AddDestination(addr);
    _checkerror(status);

    // TODO try to set higher priorities on threads
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&(sess->networkThread), &attr, &_runReceiveThread, sess);

    return sess;
}
