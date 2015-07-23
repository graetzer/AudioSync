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
#define AUDIOSTREAM_APP_NAME ((const uint8_t*)"ADST")

#define AUDIOSTREAM_PACKET_MEDIAFORMAT 1
// Try to parse the string returned from AMediaFormat_toString and use all the parameters
AMediaFormat *audiostream_createFormat(char *formatString);

#define AUDIOSTREAM_PACKET_CLOCKOFFSET 2
typedef struct {
    int64_t offsetSeconds;// This represents the number of whole seconds of elapsed time.
    int64_t offetUSeconds;//  This is the fraction of a second, represented as the number of microseconds. It is always less than one million.
} __attribute__ ((__packed__)) audiostream_clockOffset;

#ifdef __cplusplus
}
#endif
#endif //AUDIOSYNC_APPPACKET_H
