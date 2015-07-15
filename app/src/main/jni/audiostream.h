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

// Opaque pointer
// TODO make it private, use the init method to return an instance, alternatively just use a c++ class
struct audiosync_context {
    /*struct sockaddr_in* client_addr[128];
    long timeDiff[128];
    size_t numClients = 0;*/

    bool isRunning;
    pthread_t networkThread, ntpThread;

    struct timeval clockOffset;

    AMediaExtractor *extractor;
    /*uint8_t *buffer;
    size_t bufferLength;*/
    /*
    // 16 bit per sample. Assume 44,1 kHz
    // Let's buffer for 3 seconds. Use this like a ringbuffer
#define FRAME_BUFFER_SIZE 44100 * 3
    uint16_t* frameBuffer;
    uint16_t samples[];*/
};

//struct audiosync_context;
typedef struct audiosync_context  audiosync_context;

void audiostream_init();
void audiostream_deinit();

// TODO implement this through a RTPSession subclass, see example4.cpp
//void audiostream_addClient(audiosync_context_t*, const char* host);
void audiosync_startSending(audiosync_context* ctx, AMediaExtractor *extractor);
// TODO interrupted callback
void audiosync_startReceiving(audiosync_context*);
//void audiosync_startReceiving(audiosync_context_t*, const char*host);
void audiosync_stop(audiosync_context *);

#ifdef __cplusplus
}
#endif
#endif