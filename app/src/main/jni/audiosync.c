/*
 * audiosync.c: Implements the synchronization steps
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

// #include "stream.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <netdb.h>
#include <pthread.h>
#include <android/log.h>

#include "libmsntp/libmsntp.h"
#include "audiosync.h"

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioSync", __VA_ARGS__)
#define SNTP_PORT 32442
#define CONTROL_PORT 32443
#define DATA_PORT 32444
#define BUFSIZE 4096

typedef struct {

} ssad;

void audiosync_addClient(audiosync_context_t*ctx, const char* host)
{
}

void* _controlNetworkRoutine(void *ctxPtr) {
    audiosync_context_t *ctx = (audiosync_context_t *)ctxPtr;

    uint8_t buffer[BUFSIZE];
    while(true) {
        usleep(10*1000);// Sleep 10ms
    }

    return NULL;
}

// Packet format: [2 bytes, sequence number][]

void* _dataNetworkRoutine(void *ctxPtr) {
    audiosync_context_t *ctx = (audiosync_context_t *)ctxPtr;

    uint8_t buffer[BUFSIZE];
    while(true) {
        usleep(10*1000);// Sleep 10ms
    }

    return NULL;
}

void* _serveNTPRoutine(void *ctxPtr) {
    audiosync_context_t *ctx = (audiosync_context_t *)ctxPtr;

    if (msntp_start_server(SNTP_PORT) != 0)
        return NULL;

    printf("Listening for SNTP clients on port %d...", SNTP_PORT);
    while (ctx->isRunning) {
    int ret = msntp_serve();
    if (ret > 0 || ret < -1)
      return NULL;
    }

    msntp_stop_server();
    return NULL;
}

void _setupStreamingThreads(audiosync_context_t *ctx) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&(ctx->controlThread), &attr, _controlNetworkRoutine, ctx);

    pthread_attr_init(&attr);
    pthread_create(&(ctx->controlThread), &attr, _dataNetworkRoutine, ctx);
}

void audiosync_startSending(audiosync_context_t* ctx, void* todo) {
    ctx->isRunning = true;
    _setupStreamingThreads(ctx);
    pthread_create(&(ctx->ntpThread), NULL, _serveNTPRoutine, ctx);
}

void audiosync_startReceiving(audiosync_context_t *ctx, const char* host, void* todo) {
    ctx->isRunning = true;

    _setupStreamingThreads(ctx);
}

void audiosync_stop(audiosync_context_t *ctx) {
    if (ctx->isRunning) {
        ctx->isRunning = false;

        if(ctx->controlThread && pthread_join(ctx->controlThread, NULL)) {
            debugLog("Error joining thread");
        }
        if(ctx->dataThread && pthread_join(ctx->dataThread, NULL)) {
            debugLog("Error joining thread");
        }

        if(ctx->ntpThread && pthread_join(ctx->dataThread, NULL)) {
             debugLog("Error joining thread");
        }
    }
}

