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

#include <stdbool.h>
#include <pthread.h>

// Opaque pointer
typedef struct audiosync_context {
    struct sockaddr_in* client_addr[128];
    size_t numClients;

    int controlFd, dataFd;
    bool isRunning, isMaster;
    pthread_t controlThread, dataThread, ntpThread;
    struct timeval clockOffset;

    // 16 bit per sample. Assume 44,1 kHz
    // Let's buffer for 3 seconds. Use this like a ringbuffer
    #define FRAME_BUFFER_SIZE 44100 * 3
    uint16_t* frameBuffer;
    uint16_t samples[];
} audiosync_context_t;

void audiosync_addClient(audiosync_context_t*, const char* host);
void audiosync_startSending(audiosync_context_t*, void* todo);
// TODO interrupted callback
void audiosync_startReceiving(audiosync_context_t*, const char*, void *);
void audiosync_stop(audiosync_context_t *);

#ifdef __cplusplus
}
#endif
#endif