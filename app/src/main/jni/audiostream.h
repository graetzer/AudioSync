/*
 * audiosync.h: Implements the synchronization steps
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef AUDIOSYNC_STREAM
#define AUDIOSYNC_STREAM
#ifdef __cplusplus
extern "C" {
#endif

#include <media/NdkMediaExtractor.h>

//struct audiostream_context;
typedef struct audiostream_context  audiostream_context;

audiostream_context * audiostream_new();
void audiostream_free(audiostream_context *ctx);

void audiostream_startStreaming(audiostream_context* ctx, uint16_t portbase, AMediaExtractor *extractor);
// TODO interrupted callback
void audiostream_startReceiving(audiostream_context *ctx, const char *host, uint16_t portbase);
void audiostream_stop(audiostream_context *ctx);

#ifdef __cplusplus
}
#endif
#endif