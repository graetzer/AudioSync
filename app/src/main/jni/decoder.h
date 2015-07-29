/*
 * decoder.h: Decode an audio file, put the PCM data into a buffer
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef AUDIOSYNC_DECODER_H
#define AUDIOSYNC_DECODER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>

/*
 * Hold all decoded parameters
 */
struct decoder_audio {
    int32_t sampleRate, numChannels;
    ssize_t pcmLength;
    uint8_t *pcm;
};

/*
 * Not needed currently, for testing playback only
*/
struct decoder_audio decoder_decodeFile(int fd, off64_t offset, off64_t length);

// ========================== Helper functions for streaming extracting ==========================

AMediaExtractor *decoder_createExtractor(int fd, off64_t offset, off64_t fileSize);

ssize_t decoder_extractData(AMediaExtractor *extractor, uint8_t *buffer, size_t capacity,
                            int64_t *timeUSec);

// =========================== Helper functions for streaming decoding ===========================

int decoder_enqueueBuffer(AMediaCodec *codec, uint8_t *inBuffer, ssize_t inSize, int64_t time);
/*
 * Dequeue a buffer from the codec
 * @param  sinkFunc  the pcm data will be passed to this function pointer
 * @return  true if there is still data coming, false if there is no more
 */
bool decoder_dequeueBuffer(AMediaCodec *codec,
                           void (*sinkFunc)(const uint8_t *pcmBuffer, size_t pcmSize,
                                            int64_t playbackTime));

#ifdef __cplusplus
}
#endif
#endif