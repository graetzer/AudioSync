//
// Created by Simon Grätzer on 24.07.15.
//

#ifndef AUDIOSYNC_RECEIVERSESSION_H
#define AUDIOSYNC_RECEIVERSESSION_H

#include <media/NdkMediaCodec.h>
#include "AudioStreamSession.h"
#include "jrtplib/rtpaddress.h"
#include "jrtplib/rtcpapppacket.h"

class ReceiverSession : public AudioStreamSession {
public:
    ~ReceiverSession() {
        if (codec) AMediaCodec_delete(codec);
    }

    static AudioStreamSession *StartReceiving(const char *host, uint16_t portbase);

protected:
    void RunNetwork();

    AMediaCodec *codec;

    void SetFormat(AMediaFormat *newFormat);

    void SendClockOffset(int64_t offsetUSecs);

    void OnAPPPacket(jrtplib::RTCPAPPPacket *apppacket, const jrtplib::RTPTime &receivetime,
                     const jrtplib::RTPAddress *senderaddress);

    static void *RunNetworkThread(void *ctx);

    static void *RunNTPClient(void *ctx);

};

#endif //AUDIOSYNC_RECEIVERSESSION_H
