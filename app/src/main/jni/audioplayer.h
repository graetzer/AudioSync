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
void audioplayer_initGlobal(uint32_t samplesPerSec, uint32_t framesPerBuffer);
/**
 * Init player with samples and channels for the current audiostream. Buffer can fit 5 minutes
 */
void audioplayer_initPlayback(uint32_t samplesPerSec, uint32_t numChannels);
/**
 * Enqueue 16 bit PCM audio frames, channels interleaved
 */
void audioplayer_enqueuePCMFrames(const uint8_t *pcmBuffer, size_t pcmSize, int64_t playbackTimeUs);
/**
 * Synchronize Playback to an External Source
 * @param playbackTimeUs  The precise time at which to match playback of the audio-stream.
 * @param hostTimeUs      The host time at which to synchronize playback.
 */
void audioplayer_alignPlayback(int64_t playbackTimeUs, int64_t hostTimeUs);
/**
 * If positive, the server clock is ahead of the local clock;
 * if negative, the server clock is behind the local clock.
 */
void audioplayer_setHostTimeOffset(int64_t offsetUs);
/**
 * Stop playback
 */
void audioplayer_stopPlayback();
/**
 * Cleanup resources
 */
void audioplayer_cleanup();

// TODO register an error callback or at least return false

#ifdef __cplusplus
}
#endif
#endif