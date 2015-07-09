/*
 * decoder.c: Decode an audio file, put the PCM data into a buffer
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <sys/mman.h>
#include <android/log.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "Decoder", __VA_ARGS__)
#define INITIAL_BUFFER (256*256)

bool startsWith(const char *pre, const char *str) {
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

void decodeTrack(AMediaExtractor *extractor, AMediaFormat *format, uint8_t **pcmOut, const char* mime_type) {
    size_t pcmOutLength = INITIAL_BUFFER;
    size_t pcmOutMark = 0;
    *pcmOut = (uint8_t*) malloc(pcmOutLength);


    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime_type);
    int status = AMediaCodec_configure(codec, format, NULL, NULL, 0);
    assert(status == AMEDIA_OK);

    status = AMediaCodec_start(codec);
    assert(status == AMEDIA_OK);

    bool hasInput = true, hasOutput = true;
    // Decoding loop
    while(hasInput || hasOutput) {

        if (hasInput) {
            ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(codec, 5000);
            if (bufIdx >= 0) {
                size_t capacity;
                uint8_t *buffer = AMediaCodec_getInputBuffer(codec, bufIdx, &capacity);
                ssize_t written = AMediaExtractor_readSampleData(extractor, buffer, capacity);
                int64_t time = AMediaExtractor_getSampleTime(extractor);
                if (written < 0) {
                    debugLog("input EOS");
                    written = 0;
                    hasInput = false;
                }
                uint32_t flags = hasInput ? 0 : AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
                status = AMediaCodec_queueInputBuffer(codec, bufIdx, 0, written, time, flags);
                assert(status == AMEDIA_OK);
                AMediaExtractor_advance(extractor);// Next sample
            }
        }

        if (hasInput) {
            AMediaCodecBufferInfo info;
            ssize_t bufIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 1);
            if (bufIdx >= 0) {

                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                    debugLog("output EOS");
                    hasOutput = false;
                }
                size_t out_size;
                uint8_t *buffer = AMediaCodec_getInputBuffer(codec, bufIdx, &out_size);

                // In case our out buffer is too small, make it bigger
                if (out_size > pcmOutLength - pcmOutMark) {
                    pcmOutLength *= 2;
                    *pcmOut = (uint8_t*) realloc(*pcmOut, pcmOutLength);
                }
                memcpy(*pcmOut + pcmOutMark, buffer, out_size);
                pcmOutMark += out_size;

                status = AMediaCodec_releaseOutputBuffer(codec, bufIdx, true);
                assert(status == AMEDIA_OK);
            }
        }
    }

    status = AMediaCodec_stop(codec);
    assert(status == AMEDIA_OK);

    status = AMediaCodec_delete(codec);
    assert(status == AMEDIA_OK);
}

ssize_t decodeAudiofile(int fd, off_t fileSize, uint8_t **pcmOut, uint32_t *bitRate, uint32_t *sampleRate) {

    AMediaExtractor *extractor = AMediaExtractor_new();
    media_status_t status = AMediaExtractor_setDataSourceFd(extractor, fd, 0, fileSize);
    if (status != AMEDIA_OK) {
        debugLog("Error opening file");
        return -1;
    }

    // Find the audio track
    size_t tracks = AMediaExtractor_getTrackCount(extractor);
    size_t idx = 0;
    for (; idx < tracks; idx++) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(extractor, idx);
        const char *mime_type;
        bool sucess = !!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime_type);
        if (startsWith("audio/", mime_type)) {
            // We got a winner
            status = AMediaExtractor_selectTrack(extractor, idx);
            assert(status == AMEDIA_OK);

            // Extract relevant data
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitRate);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, sampleRate);

            decodeTrack(extractor, format, pcmOut, mime_type);
            AMediaFormat_delete(format);
            break;
        }
        AMediaFormat_delete(format);
    }
}