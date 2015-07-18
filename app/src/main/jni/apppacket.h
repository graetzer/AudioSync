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
extern const uint8_t *audiostream_app_name;

#define AUDIOSTREAM_PACKET_APP_MEDIAFORMAT 1
// Try to parse the string returned from AMediaFormat_toString and use all the parameters
AMediaFormat *audiostream_createFormat(char *formatString);

/*typedef struct {
    int32_t samplesPerSec;
    int32_t numChannels;
    char mime[64];// Maybe we should make this one bigger
} __attribute__ ((__packed__)) audiostream_packet_format;*/

#define AUDIOSTREAM_PACKET_APP_CLOCK 2
typedef struct {
    uint32_t ssrc;
    int32_t clockOffset;
} __attribute__ ((__packed__)) audiostream_packet_clock;

#ifdef __cplusplus
}
#endif
#endif //AUDIOSYNC_APPPACKET_H
