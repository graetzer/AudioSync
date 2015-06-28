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

#ifndef _Included_AudioSync_stream
#define _Included_AudioSync_stream

#include "fifo.h"

// Opaque pointer
typedef struct audiosync_context;

void audiosync_addClient(audiosync_context*, const char* host);
void audiosync_startSending(audiosync_context*, void* todo);
void audiosync_startReceiving(audiosync_context*, const char*, audio_utils_fifo *);
void audiosync_stop(audiosync_context *);

##endif