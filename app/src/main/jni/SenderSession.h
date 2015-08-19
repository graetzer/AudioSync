//
// Created by Simon Grätzer on 24.07.15.
//

#ifndef AUDIOSYNC_SENDERSESSION_H
#define AUDIOSYNC_SENDERSESSION_H

#include <atomic>
#include <media/NdkMediaExtractor.h>
#include "AudioStreamSession.h"
#include "jrtplib/rtpaddress.h"
#include "jrtplib/rtcpapppacket.h"

class SenderSession : public AudioStreamSession {
public:
    ~SenderSession() {
        if (extractor) AMediaExtractor_delete(extractor);
    }

    static SenderSession *StartStreaming(uint16_t portbase, AMediaExtractor *extractor);

    bool IsSender() {
        return true;
    }

    int64_t CurrentPlaybackTimeUs();

protected:

    void OnNewSource(jrtplib::RTPSourceData *dat);

    void OnBYEPacket(jrtplib::RTPSourceData *dat);

    void OnRemoveSource(jrtplib::RTPSourceData *dat);

    void OnAPPPacket(jrtplib::RTCPAPPPacket *apppacket, const jrtplib::RTPTime &receivetime,
                     const jrtplib::RTPAddress *senderaddress);

private:
    int64_t playbackStartUs = 0;
    std::atomic_int connectedSources;
    AMediaExtractor *extractor;

    void RunNetwork();
    /*void SendPacketRecursive(const void *data, size_t len, uint8_t pt, bool mark,
                             uint32_t timestampinc);*/
    int64_t transmissionLatency();
    jrtplib::RTPAddress *addressFromData(jrtplib::RTPSourceData *dat);

    static void *RunNetworkThread(void *ctx);

    static void *RunNTPServer(void *ctx);
};


#endif //AUDIOSYNC_SENDERSESSION_H
