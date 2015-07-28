//
// Created by Simon Grätzer on 24.07.15.
//

#include "SenderSession.h"

#include <android/log.h>

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioSync", __VA_ARGS__)

#include "libmsntp/libmsntp.h"
#include "jrtplib/rtppacket.h"
#include "jrtplib/rtpsourcedata.h"
#include "jrtplib/rtpudpv4transmitter.h"
#include "jrtplib/rtpipv4address.h"
#include "jrtplib/rtpipv6address.h"
#include "jrtplib/rtpsessionparams.h"

#include "decoder.h"
#include "apppacket.h"

#define PACKET_GAP_MICRO 2000
using namespace jrtplib;

static void _checkerror(int rtperr) {
    if (rtperr < 0) {
        debugLog("RTP Error: %s", RTPGetErrorString(rtperr).c_str());
        pthread_exit(0);
    }
}

void SenderSession::RunNetwork() {
    // Waiting for connections and sending them data
    if (!extractor) {
        log("No datasource");
        return;
    }
    /*if (format == NULL) {
        int idx = AMediaExtractor_getSampleTrackIndex(extractor);// If this returns -1, we crash
        format = AMediaExtractor_getTrackFormat(extractor, (size_t)idx);
        debugLog("")
    }*/

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
        uint8_t buffer[8192];// TODO figure out optimum size
        written = decoder_extractData(extractor, buffer, sizeof(buffer), &time);
        if (lastTime == -1) lastTime = time;
        uint32_t timestampinc = (uint32_t) (time - lastTime);// Assuming it will fit
        lastTime = time;

        if (written >= 0) {
            if (written > 1024) {
                log("This package is too large: %ld, split it up. (%.2fs)", (long) written,
                    time/1000000.0);
                // TODO these UDP packages are definitely too large and result in IP fragmentation
                // most MTU's will be 1500, RTP header is 12 bytes we should
                // split the packets up at some point(1024 seems reasonable)
                //SendPacketRecursive(buffer, (size_t) written, 0, false, timestampinc);

            } //else
            status = SendPacket(buffer, (size_t) written, 0, false, timestampinc);
        } else {
            buffer[0] = '\0';
            status = SendPacket(buffer, 1, 0, true, timestampinc);
            log("Sender: End of stream.");
        }
        _checkerror(status);

        // Don't decrease the waiting time too much, it seems sending a great number
        // of packets very fast, will cause the network (or the client) to drop a high
        // number of these packets. In any case the client needs to mitigate this.
        // TODO auto-adjust this value based on lost packets
        // TODO figure out how to utilize throughput
        RTPTime::Wait(RTPTime(0, timestampinc));

        // Not really necessary, we are not using this
        BeginDataAccess();

        // check incoming packets
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

    if (GotoFirstSource()) {
        do {
            int32_t lost = GetCurrentSourceInfo()->RR_GetPacketsLost();
            log("Source lost %d packets", lost);
        } while (GotoNextSource());
    }

    log("I'm done sending");
    BYEDestroy(RTPTime(2, 0), 0, 0);
}

/*void SenderSession::SendPacketRecursive(const void *data, size_t len, uint8_t pt, bool mark,
                                        uint32_t timestampinc) {
    const size_t maxSize = 1024;
    if (len > maxSize) {
        SendPacket(data, maxSize, pt, mark, timestampinc);
        RTPTime::Wait(RTPTime(0, PACKET_GAP_MICRO));
        SendPacketRecursive(data+maxSize, len - maxSize, pt, mark, 0);
    } else {
        SendPacket(data, len, pt, mark, timestampinc);
    }
}*/

RTPAddress *SenderSession::addressFromData(RTPSourceData *dat) {
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

void SenderSession::OnNewSource(jrtplib::RTPSourceData *dat) {
    if (dat->IsOwnSSRC()) return;

    log("Added new source");
    RTPAddress *dest = addressFromData(dat);
    if (dest) {
        AddDestination(*dest);
        delete(dest);
        connectedSources++;

        if (this->format) {
            const char *formatString = AMediaFormat_toString(format);
            SendRTCPAPPPacket(AUDIOSTREAM_PACKET_MEDIAFORMAT, AUDIOSTREAM_APP_NAME,
                              (const uint8_t *) formatString, strlen(formatString));
        }
    }
}

void SenderSession::OnBYEPacket(jrtplib::RTPSourceData *dat) {
    if (dat->IsOwnSSRC()) return;

    log("Received bye package");
    RTPAddress *dest = addressFromData(dat);
    if (dest != NULL) {
        DeleteDestination(*dest);
        delete(dest);
        connectedSources--;
    }
}

void SenderSession::OnRemoveSource(jrtplib::RTPSourceData *dat) {
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

void SenderSession::OnAPPPacket(RTCPAPPPacket *apppacket, const RTPTime &receivetime,
                                const RTPAddress *senderaddress) {
    if (apppacket->GetSubType() == AUDIOSTREAM_PACKET_CLOCKOFFSET
        && apppacket->GetAPPDataLength() >= sizeof(audiostream_clockOffset)) {
        audiostream_clockOffset *clock = (audiostream_clockOffset *) apppacket->GetAPPData();
        debugLog("Received clockoffser: %ld.%6ld", clock->offsetSeconds, clock->offetUSeconds);

        RTPSourceData *source = GetSourceInfo(apppacket->GetSSRC());
        if (source) {

        }
        // TODO
    }
}

void * SenderSession::RunNetworkThread(void *ctx) {
    ((SenderSession *) ctx)->RunNetwork();
    return NULL;
}

RTPUDPv4TransmissionParams transparams;
void * SenderSession::RunNTPServer(void *ctx) {
    SenderSession *sess = (SenderSession *) ctx;
    int port = transparams.GetPortbase() + AUDIOSYNC_SNTP_PORT_OFFSET;
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

SenderSession * SenderSession::StartStreaming(uint16_t portbase, AMediaExtractor *extractor) {
    SenderSession *sess = new SenderSession();
    RTPSessionParams sessparams;

    // IMPORTANT: The local timestamp unit MUST be set, otherwise
    //            RTCP Sender Report info will be calculated wrong
    // In this case, we'll be sending 10 samples each second, so we'll
    // put the timestamp unit to (1.0/10.0)
    sessparams.SetOwnTimestampUnit(AUDIOSYNC_TIMESTAMP_UNITS);
    sessparams.SetReceiveMode(RTPTransmitter::ReceiveMode::AcceptAll);
    //sessparams.SetAcceptOwnPackets(false);
    transparams.SetPortbase(portbase);
    int status = sess->Create(sessparams, &transparams);
    _checkerror(status);

    sess->SetDefaultMark(false);
    sess->extractor = extractor;
    pthread_create(&(sess->networkThread), NULL, &(SenderSession::RunNetworkThread), sess);
    pthread_create(&sess->ntpThread, NULL, &SenderSession::RunNTPServer, sess);

    debugLog("Started RTP server on port %u, now waiting for clients", portbase);
    return sess;
}