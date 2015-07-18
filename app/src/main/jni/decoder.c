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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include "decoder.h"

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "Decoder", __VA_ARGS__)
#define INITIAL_BUFFER (1024*1024 * 4) // 4MB

bool startsWith(const char *pre, const char *str) {
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

ssize_t _decodeTrack(AMediaExtractor *extractor, AMediaFormat *format, uint8_t **pcmOut, int32_t *sampleRate, int32_t *numChannels) {
    size_t pcmOutLength = INITIAL_BUFFER;
    size_t pcmOutMark = 0;
    uint8_t* pcmBuffer = (uint8_t*) malloc(pcmOutLength);

    const char *mime_type;
    AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime_type);
    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime_type);

    int status = AMediaCodec_configure(codec, format, NULL, NULL, 0);
    if(status != AMEDIA_OK) return -1;
    status = AMediaCodec_start(codec);
    if(status != AMEDIA_OK) return -1;

    format = AMediaCodec_getOutputFormat(codec);
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
                    hasInput = false;
                    status = AMediaCodec_queueInputBuffer(codec, bufIdx, 0, 0, time, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                } else {
                    status = AMediaCodec_queueInputBuffer(codec, bufIdx, 0, written, time, 0);
                }
                if(status != AMEDIA_OK) return -1;
                AMediaExtractor_advance(extractor);// Next sample
            }
        }

        if (hasOutput) {
            AMediaCodecBufferInfo info;
            ssize_t bufIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 5000);
            if (bufIdx >= 0) {

                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                    debugLog("output EOS");
                    hasOutput = false;
                }

                uint8_t *buffer = AMediaCodec_getOutputBuffer(codec, bufIdx, NULL);
                //debugLog("BufferInfo offset: %d; size: %d; outsize: %lu", info.offset, info.size, (unsigned long)out_size);
                // In case our out buffer is too small, make it bigger
                if (pcmOutLength - pcmOutMark < info.size) {
                    pcmOutLength += info.size + INITIAL_BUFFER/2;
                    pcmBuffer = (uint8_t*) realloc(pcmBuffer, pcmOutLength);
                }
                memcpy(pcmBuffer + pcmOutMark, buffer + info.offset, info.size);
                pcmOutMark += info.size;

                status = AMediaCodec_releaseOutputBuffer(codec, bufIdx, false);
                if(status != AMEDIA_OK) return -1;
            } else if (bufIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat_delete(format);
                format = AMediaCodec_getOutputFormat(codec);
                debugLog("Output format changed to: %s", AMediaFormat_toString(format));
            }
        }
    }

    // Extract the final parameters
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, sampleRate);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, numChannels);
    AMediaFormat_delete(format);

    // Not sure if it is necessary to check the status here
    status = AMediaCodec_stop(codec);
    if(status != AMEDIA_OK) return -1;
    status = AMediaCodec_delete(codec);
    if(status != AMEDIA_OK) return -1;

    *pcmOut = pcmBuffer;
    return pcmOutMark;
}

struct decoder_audio decoder_decodeFile(int fd, off64_t offset, off64_t fileSize) {
    struct decoder_audio result = {0, 0, -1, 0};

    AMediaExtractor *extractor = AMediaExtractor_new();
    media_status_t status = AMediaExtractor_setDataSourceFd(extractor, fd, offset, fileSize);
    if (status != AMEDIA_OK) {
        debugLog("Error opening file");
        return result;
    }

    // Find the audio track
    size_t tracks = AMediaExtractor_getTrackCount(extractor);
    for (size_t idx = 0; idx < tracks; idx++) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(extractor, idx);
        const char *mime_type;
        bool success = AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime_type);
        if (success && startsWith("audio/", mime_type)) {// We got a winner
            debugLog("Input format %s", AMediaFormat_toString(format));

            status = AMediaExtractor_selectTrack(extractor, idx);
            if(status != AMEDIA_OK) break;

            // Extract relevant data
            result.pcmLength = _decodeTrack(extractor, format, &(result.pcm),
                                                                &(result.sampleRate),
                                                                &(result.numChannels));
            AMediaFormat_delete(format);
            break;
        }
        AMediaFormat_delete(format);
    }
    return result;
}

AMediaExtractor* decoder_createExtractor(int fd, off64_t offset, off64_t fileSize) {

    AMediaExtractor *extractor = AMediaExtractor_new();
    media_status_t status = AMediaExtractor_setDataSourceFd(extractor, fd, offset, fileSize);
    if (status != AMEDIA_OK) {
        debugLog("Error opening file");
        return NULL;
    }

    // Find the audio track
    size_t tracks = AMediaExtractor_getTrackCount(extractor);
    for (size_t idx = 0; idx < tracks; idx++) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(extractor, idx);
        const char *mime_type;
        bool success = AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime_type);
        if (success && startsWith("audio/", mime_type)) {// We got a winner
            debugLog("Input format %s", AMediaFormat_toString(format));
            status = AMediaExtractor_selectTrack(extractor, idx);
            if(status != AMEDIA_OK) return NULL;



            AMediaFormat_delete(format);
            break;
        }
        AMediaFormat_delete(format);
    }
    return extractor;
}

ssize_t decoder_extractData(AMediaExtractor* extractor, uint8_t *buffer, size_t capacity, int64_t *time) {
    ssize_t written = AMediaExtractor_readSampleData(extractor, buffer, capacity);
    *time = AMediaExtractor_getSampleTime(extractor);
    AMediaExtractor_advance(extractor);// Next sample
    return written;
}

int decoder_enqueueBuffer(AMediaCodec *codec, uint8_t *inBuffer, ssize_t inSize, int64_t time) {
    ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(codec, 5000);
    if (bufIdx >= 0) {
        size_t capacity;
        uint8_t *buffer = AMediaCodec_getInputBuffer(codec, bufIdx, &capacity);
        int status;
        if (inSize < 0) {
            debugLog("input EOS");
            status = AMediaCodec_queueInputBuffer(codec, bufIdx, 0, 0, time, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        } else {
            if (capacity < inSize) {
                memcpy(buffer, inBuffer, capacity);
                status = AMediaCodec_queueInputBuffer(codec, bufIdx, 0, capacity, time, 0);
                if(status != AMEDIA_OK) return status;
                status = decoder_enqueueBuffer(codec, buffer + capacity, inSize - capacity, time);
            } else {
                memcpy(buffer, inBuffer, (size_t)inSize);
                status = AMediaCodec_queueInputBuffer(codec, bufIdx, 0, inSize, time, 0);
            }
        }
        if(status != AMEDIA_OK) return status;
    }
    return AMEDIA_OK;
}

bool decoder_dequeueBuffer(AMediaCodec *codec, AMediaFormat **format, uint8_t **pcmBuffer, size_t *pcmSize, size_t *pcmOffset) {
    AMediaCodecBufferInfo info;
    ssize_t bufIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 5000);
    if (bufIdx >= 0) {

        uint8_t *buffer = AMediaCodec_getOutputBuffer(codec, bufIdx, NULL);
        // In case our out buffer is too small, make it bigger
        if (*pcmSize < *pcmOffset + info.size) {
            *pcmSize += info.size + 512*256;
            *pcmBuffer = (uint8_t*) realloc(*pcmBuffer, *pcmSize);
        }

        memcpy(*pcmBuffer + *pcmOffset, buffer + info.offset, (size_t)info.size);
        *pcmOffset += info.size;

        int status = AMediaCodec_releaseOutputBuffer(codec, bufIdx, false);
        if(status != AMEDIA_OK) return false;
    } else if (bufIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        if (*format != NULL) AMediaFormat_delete(*format);
        *format = AMediaCodec_getOutputFormat(codec);
        debugLog("Output format changed to: %s", AMediaFormat_toString(*format));
    }

    return (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
}