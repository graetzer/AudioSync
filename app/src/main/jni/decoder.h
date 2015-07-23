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

struct decoder_audio {
    int32_t sampleRate, numChannels;
    ssize_t pcmLength;
    uint8_t *pcm;
};

/*

*/
struct decoder_audio decoder_decodeFile(int fd, off64_t offset, off64_t length);


AMediaExtractor* decoder_createExtractor(int fd, off64_t offset, off64_t fileSize);
ssize_t decoder_extractData(AMediaExtractor* extractor, uint8_t *buffer, size_t capacity, int64_t *time);

int decoder_enqueueBuffer(AMediaCodec *codec, uint8_t *inBuffer, ssize_t inSize, int64_t time);
/*
 * Dequeue a buffer from the codec
 * @param  outIdx  the index of the buffer which was dequeued, pass in the pointer to the index
 *         of the previous call. On the first call pass a pointer to a var with content (-1)
 * @return  The info struct for the dequeued buffer
 */
AMediaCodecBufferInfo decoder_dequeueBuffer(AMediaCodec *codec, uint8_t **pcmBuffer, ssize_t *outIdx);

#ifdef __cplusplus
}
#endif
#endif