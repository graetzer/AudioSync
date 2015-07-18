//
// Created by Simon Grätzer on 18.07.15.
//

#ifndef AUDIOSYNC_APPPACKET_H
#define AUDIOSYNC_APPPACKET_H

// Packettype should be indicated through the subtype in the RC field
#define AUDIOSTREAM_APP_NAME ("AUST")

#define AUDIOSTREAM_PACKET_APP_FORMAT 1
typedef struct {
    int32_t samplesPerSec;
    int32_t numChannels;
    char mime[1];// Length can be calculated through the length of the packet
} __attribute__ ((__packed__)) audiostream_packet_format;

#define AUDIOSTREAM_PACKET_APP_CLOCK 2
typedef struct {
    uint32_t ssrc;
    int32_t clockOffset;
} __attribute__ ((__packed__)) audiostream_packet_clock;

#endif //AUDIOSYNC_APPPACKET_H
