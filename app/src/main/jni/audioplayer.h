/*
 * audioplayer.h: Encapsulate the OpenSL interface into something simpler to use
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef AUDIOSYNC_AUDIOPLAYER_H
#define AUDIOSYNC_AUDIOPLAYER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Initialize the OpenSL audio interface
void audioplayer_init(int32_t samplesPerSec, int32_t framesPerBuffer);
void audioplayer_startPlayback(uint8_t *buffer, size_t bufferSize, int32_t samplesPerSec, int32_t numChannels);
//void audioplayer_enqueuePCM(uint8_t *pcmBuffer, size_t pcmSize);
void audioplayer_stopPlayback();
void audioplayer_cleanup();


#ifdef __cplusplus
}
#endif
#endif