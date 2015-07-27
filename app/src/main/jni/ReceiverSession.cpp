//
// Created by Simon Grätzer on 24.07.15.
//

#include "ReceiverSession.h"

#include <android/log.h>

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "ReceiverSession", __VA_ARGS__)

#include "libmsntp/libmsntp.h"
#include "jrtplib/rtppacket.h"
#include "jrtplib/rtpsourcedata.h"
#include "jrtplib/rtpudpv4transmitter.h"
#include "jrtplib/rtpipv4address.h"
#include "jrtplib/rtpsessionparams.h"

#include "apppacket.h"
#include "audioplayer.h"
#include "decoder.h"
#include "UIStats.h"

using namespace jrtplib;

static void _checkerror(int rtperr) {
    if (rtperr < 0) {
        debugLog("RTP Error: %s", RTPGetErrorString(rtperr).c_str());
        pthread_exit(0);
    }
}


void ReceiverSession::RunNetwork() {
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
        RTPTime::Wait(RTPTime(1, 0));// Wait 1s
    }
    if (codec == NULL) {
        log("No codec set");
        return;
    }

    // Start decoder
    status = AMediaCodec_start(codec);
    if (status != AMEDIA_OK) return;
    log("Started decoder");

    // Extracting format data
    int32_t samples = 44100, channels = 1;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &samples);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
    audioplayer_initPlayback((uint32_t) samples, (uint32_t) channels);

    bool hasInput = true, hasOutput = true;
    int32_t beginTimestamp = -1, lastTimestamp = 0;
    uint16_t lastSeqNum = 0;
    while (hasInput && isRunning) {
        BeginDataAccess();
        clearRtpSources();
        if (GotoFirstSourceWithData()) {
            do {

                RTPSourceData * sourceData = GetCurrentSourceInfo();

                size_t nameLen = 0;
                uint8_t *nameData = sourceData->SDES_GetCNAME(&nameLen);
                std::string name(reinterpret_cast<const char*>(nameData), nameLen);

                RTPSourceInfo info = {
                    name, // name
                    sourceData->INF_GetJitter(), // jitter
                    sourceData->RR_GetPacketsLost(), // packet loss
                    0 // time offset
                };

                RTPPacket *pack;
                while ((pack = GetNextPacket()) != NULL) {
                    // We repurposed the marker flag as end of file
                    hasInput = !pack->HasMarker();

                    // Calculate playback time and do some lost package corrections
                    uint32_t timestamp = pack->GetTimestamp();
                    if (beginTimestamp == -1) {// record first timestamp and use differences
                        beginTimestamp = timestamp;
                        lastSeqNum = pack->GetSequenceNumber() - (uint16_t) 1;
                    }
                    timestamp -= beginTimestamp;
                    // Handle lost packets, TODO How does this work with multiple senders?
                    if (pack->GetSequenceNumber() == lastSeqNum + 1) {
                        // TODO handle mutliple packages where data should be put in the codec at
                        // the same time
                        /*if (timestamp == lastTimestamp) {

                        } else {

                        }*/
                    } else {
                        log("Packets jumped %u => %u | %.2f => %.2fs.", lastSeqNum,
                        pack->GetSequenceNumber(), lastTimestamp / 1000000.0,
                        timestamp / 1000000.0);
                        // TODO evaluate the impact of this time gap parameter
                        if (timestamp - lastTimestamp > 50000) {//50 ms
                            // According to the docs we need to flushIf data is not adjacent.
                            // It is unclear how big these gaps can be and still be tolerable.
                            // During testing this call did cause the codec
                            // to throw errors. most likely in combination with splitted packages,
                            // where one of a a set of packages with the same timestamp got lost
                            log("Flushing codec");
                            AMediaCodec_flush(codec);
                        }
                    }
                    lastSeqNum = pack->GetSequenceNumber();
                    lastTimestamp = timestamp;

                    if (hasInput) {
                        //log("Received %.2f", timestamp / 1000000.0);
                        uint8_t *payload = pack->GetPayloadData();
                        size_t length = pack->GetPayloadLength();
                        status = decoder_enqueueBuffer(codec, payload, length, (int64_t) time);
                        if (status != AMEDIA_OK) hasInput = false;
                    } else {
                        log("Receiver: End of file");
                        // Tell the codec we are done
                        decoder_enqueueBuffer(codec, NULL, -1, (int64_t) time);
                    }
                    hasOutput = decoder_dequeueBuffer(codec, &audioplayer_enqueuePCMFrames);

                    DeletePacket(pack);
                }
            } while (GotoNextSourceWithData());
        }
        EndDataAccess();

        // Ok we should give other threads the opportunity to run
        RTPTime::Wait(RTPTime(0, 2000));// Wait 2000us

#ifndef RTP_SUPPORT_THREAD
        status = sess.Poll();
        checkerror(status);
#endif // RTP_SUPPORT_THREAD
    }
    log("Received all data, ending RTP session.");
    BYEDestroy(RTPTime(1, 0), 0, 0);

    while (hasOutput && status == AMEDIA_OK && isRunning) {
        hasOutput = decoder_dequeueBuffer(codec, &audioplayer_enqueuePCMFrames);
        RTPTime::Wait(RTPTime(0, 1000));// Wait 1000us
    }
    AMediaCodec_stop(codec);
    log("Finished decoding");
}

void ReceiverSession::SetFormat(AMediaFormat *newFormat) {

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

void ReceiverSession::SetClockOffset(struct timeval val) {
    audiostream_clockOffset off = {.offsetSeconds = val.tv_sec, .offetUSeconds = val.tv_usec};
    this->SendRTCPAPPPacket(AUDIOSTREAM_PACKET_CLOCKOFFSET, AUDIOSTREAM_APP_NAME, &off,
                            sizeof(audiostream_clockOffset));
}

void ReceiverSession::OnAPPPacket(RTCPAPPPacket *apppacket, const RTPTime &receivetime,
                                  const RTPAddress *senderaddress) {
    if (apppacket->GetSubType() == AUDIOSTREAM_PACKET_MEDIAFORMAT) {
        char *formatString = (char *) apppacket->GetAPPData();
        if (formatString[apppacket->GetAPPDataLength() - 1] == '\0') {
            log("Received format string %s", formatString);
            AMediaFormat *newFormat = audiostream_createFormat(formatString);

            if (newFormat != NULL) {
                log("Parsed format string %s", AMediaFormat_toString(newFormat));
                // TODO activate this again
                // TODO figure out how to compare formats and see if it's the same as the old
                if (format == NULL)
                   SetFormat(newFormat);
                else
                    AMediaFormat_delete(newFormat);
            }
        }
    }
}

void * ReceiverSession::RunNetworkThread(void *ctx) {
    ((ReceiverSession *) ctx)->RunNetwork();
    return NULL;
}

void * ReceiverSession::RunNTPClient(void *ctx) {
    ReceiverSession *sess = (ReceiverSession *) ctx;
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

AudioStreamSession *ReceiverSession::StartReceiving(const char *host, uint16_t portbase) {
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
    pthread_create(&(sess->networkThread), &attr, &ReceiverSession::RunNetworkThread, sess);

    return sess;
}