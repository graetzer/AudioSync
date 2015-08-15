/*
 * apppacket.h: subtypes, struct's and helper functions to work with custom APP RTCP packets
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef AUDIOSYNC_APPPACKET_H
#define AUDIOSYNC_APPPACKET_H

// c doesn't know the bool type used in NdkMediaFormat.h
#include <stdbool.h>
#include <media/NdkMediaFormat.h>

#ifdef __cplusplus
extern "C" {
#endif

// Packettype should be indicated through the subtype in the RC field
#define AUDIOSTREAM_APP ((const uint8_t*)"ADST")

// A media format packet is just a string, generated from an MediaFormat instance
#define AUDIOSTREAM_PACKET_MEDIAFORMAT 1
// Try to parse the string returned from AMediaFormat_toString and use all the parameters
AMediaFormat *audiostream_createFormat(char *formatString);

#define AUDIOSTREAM_PACKET_CLOCK_OFFSET 2
typedef struct {
    /**
     * Time when this was send
     */
    int64_t systemTimeUs;
    /**
     * Clock offset relative to the ntp server, in microseconds.
     * If positive, the server clock is ahead of the local clock;
     * if negative, the server clock is behind the local clock.
     */
    int64_t offsetUSeconds;
} __attribute__ ((__packed__)) audiostream_clockOffset;

#define AUDIOSTREAM_PACKET_CLOCK_SYNC 2
// Order clients to align playback at these points
typedef struct {
    int64_t systemTimeUs;// When to actually play this, derived from the senders system clock
    int64_t playbackUSeconds;// The media time derived from the RTP packet timestamps
} __attribute__ ((__packed__)) audiostream_clockSync;

// a million microseconds = one second
#define SECOND_MICRO (1000000)
inline int64_t audiosync_systemTimeUs();
uint64_t audiosync_monotonicTimeUs();

#ifdef __cplusplus
}
#endif
#endif //AUDIOSYNC_APPPACKET_H
