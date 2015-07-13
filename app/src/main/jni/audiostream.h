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

// Opaque pointer
struct audiosync_context;
typedef struct audiosync_context  audiosync_context_t;

void audiostream_init();
void audiostream_deinit();

void audiostream_addClient(audiosync_context_t*, const char* host);
void audiosync_startSending(audiosync_context_t*, void* todo);
// TODO interrupted callback
void audiosync_startReceiving(audiosync_context_t*, const char*, void *);
void audiosync_stop(audiosync_context_t *);

#ifdef __cplusplus
}
#endif
#endif