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

#include <cinttypes>

#include "decoder.h"
#include "apppacket.h"


#define PACKET_GAP_MICRO 2000
using namespace jrtplib;

static void _checkerror(int rtperr) {
    if (rtperr < 0) {
        debugLog("RTP Error: %s", RTPGetErrorString(rtperr).c_str());
        //pthread_exit(0);
    }
}

void SenderSession::RunNetwork() {
    // Waiting for connections and sending them data
    if (!extractor) {
        log("No datasource");
        return;
    }

    int status = 0;
    while (connectedSources == 0 && isRunning) {
        RTPTime::Wait(RTPTime(2, 0));// Wait 2s
        log("Waiting for clients....");
    }
    if (!isRunning) return;

    log("Client connected, starting to send in 2 seconds");
    RTPTime::Wait(RTPTime(3, 0));// Let's wait for some NTP sync's
    this->playbackStartUs = audiosync_systemTimeUs() + transmissionLatency();

    ssize_t written = 0;
    int64_t lastTimeUs = -1, lastClockSyncUs = 0;
    while (written >= 0 && isRunning) {
        int64_t timeUs = 0;
        uint8_t buffer[8192];// TODO figure out optimum size
        written = decoder_extractData(extractor, buffer, sizeof(buffer), &timeUs);
        if (lastTimeUs == -1) lastTimeUs = timeUs;// We need to calc
        uint32_t timestampinc = (uint32_t) (timeUs - lastTimeUs);// Assuming it will fit
        lastTimeUs = timeUs;

        if (written >= 0) {
            if (written > 1200) {
                log("Package is too large: %ld, split it up. (%.2fs)", (long) written, timeUs/1E6);
                // TODO these UDP packages are definitely too large and result in IP fragmentation
                // most MTU's will be 1500, RTP header is 12 bytes we should
                // split the packets up at some point(1024 seems reasonable)
                //SendPacketRecursive(buffer, (size_t) written, 0, false, timestampinc);
            }
            // Periodically send out clock syncs
            //if (timeUs - lastClockSyncUs > SECOND_MICRO) {
                int64_t usecs = htonq(this->playbackStartUs + timeUs);
                status = SendPacketEx(buffer, (size_t) written, 0, false, timestampinc,
                                      AUDIOSYNC_EXTENSION_HEADER_ID,
                                      &usecs,
                                      sizeof(int64_t) / sizeof(uint32_t));
                lastClockSyncUs = timeUs;
            //} else {
            //    status = SendPacket(buffer, (size_t) written, 0, false, timestampinc);
           // }
        } else {
            buffer[0] = '\0';
            status = SendPacket(buffer, 1, 0, true, timestampinc);// Use marker as end of data mark
            log("Sender: End of stream.");
        }
        _checkerror(status);

        // Don't decrease the waiting time too much, sending a great number of packets very fast,
        // will cause the network (or the client) to drop a high number of these packets.
        // TODO auto-adjust this value based on lost packets, figure out how to utilize throughput
        //uint32_t waitUs = timestampinc > 10000 ? timestampinc - 10000 : 2000;
        RTPTime::Wait(RTPTime(0, timestampinc/3));

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
    }
    if (written >= 0 && !isRunning) {
        debugLog("Sender was canceled before finishing streaming");
    }

    log("I'm done sending");
    BYEDestroy(RTPTime(2, 0), 0, 0);
}

int64_t SenderSession::transmissionLatency() {
    return 10 * SECOND_MICRO;;
}

/*void SenderSession::sendClockSync(int64_t playbackUSeconds) {
    // Let's put playback a little in the future, because the clients will buffer data
    // and start after this treshold was met
    int64_t maxOffsetUSec = 5 * SECOND_MICRO;
    // TODO replace with latency search, it doesn't matter how much the clocks diverge
    if (GotoFirstSource()) {
        do {// Always use the largest offset, technically we can ignore positive offsets
            // because a positive offset means the clients lags behind
            int64_t offset = (int64_t) llabs(GetCurrentSourceInfo()->GetClockOffsetUSeconds());
            if (maxOffsetUSec < offset) maxOffsetUSec = offset;
        } while(GotoNextSource());
    }

    if (playbackUSeconds == 0) {// now + maxOffset
        this->playbackStartUs = audiosync_systemTimeUs() + maxOffsetUSec;
    }
    int64_t usecs = this->playbackStartUs + playbackUSeconds;
    audiostream_clockSync sync;
    sync.systemTimeUs = htonq(usecs);
    sync.playbackTimeUs = htonq(playbackUSeconds);
    SendRTCPAPPPacket(AUDIOSTREAM_PACKET_CLOCK_SYNC, AUDIOSTREAM_APP, &sync, sizeof(audiostream_clockSync));
}*/

void SenderSession::OnAPPPacket(RTCPAPPPacket *apppacket, const RTPTime &receivetime,
                                const RTPAddress *senderaddress) {
    if (apppacket->GetSubType() == AUDIOSTREAM_PACKET_CLOCK_OFFSET
        && apppacket->GetAPPDataLength() >= sizeof(audiostream_clockOffset)) {
        audiostream_clockOffset *clock = (audiostream_clockOffset *) apppacket->GetAPPData();
        RTPSourceData *source = GetSourceInfo(apppacket->GetSSRC());
        if (source) {

            int64_t offsetUs = ntohq(clock->offsetUSeconds);
            debugLog("Received clockoffset: %fs", offsetUs/1E6);
            // Add the latency between server and client
            //offsetUs += audiosync_systemTimeUs() - ntohq(clock->systemTimeUs);
            source->SetClockOffsetUSeconds(offsetUs);
        }
    }
}

int64_t SenderSession::CurrentPlaybackTimeUs() {
    int64_t pUs = audiosync_systemTimeUs() - this->playbackStartUs;
    return pUs > 0 ? pUs : 0;// Can be negative initially
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
            SendRTCPAPPPacket(AUDIOSTREAM_PACKET_MEDIAFORMAT, AUDIOSTREAM_APP,
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

void * SenderSession::RunNetworkThread(void *ctx) {
    ((SenderSession *) ctx)->RunNetwork();
    return NULL;
}

RTPUDPv4TransmissionParams transparams;
void * SenderSession::RunNTPServer(void *ctx) {
    SenderSession *sess = (SenderSession *) ctx;
    int port = transparams.GetPortbase() + AUDIOSYNC_SNTP_PORT_OFFSET;
    if (msntp_start_server(port) != 0) {
        debugLog("Listening for SNTP clients on port %d...", port);
        return NULL;
    }


    debugLog("Listening for SNTP clients on port %d...", port);
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
    sess->SetLocalName("Sender", 6);
    sess->extractor = extractor;
    pthread_create(&(sess->networkThread), NULL, &(SenderSession::RunNetworkThread), sess);
    pthread_create(&sess->ntpThread, NULL, &SenderSession::RunNTPServer, sess);

    debugLog("Started RTP server on port %u, now waiting for clients", portbase);
    return sess;
}