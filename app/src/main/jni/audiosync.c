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

#include "stream.h"
#include "libmsntp/libsmntp.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <android/log.h>

#include "audiosync_fifo.c"

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioSync", __VA_ARGS__)
#define SNTP_PORT 32442
#define CONTROL_PORT 32443
#define DATA_PORT 32444
#define BUFSIZE 4096

typedef struct {
    struct sockaddr_in* client_addr[128];
    size_t numClients = 0;

    int controlFd, streamFd;
    bool isRunning, isMaster;
    pthread_t controlThread, dataThread, ntpThread;
    audio_utils_fifo *dataFifo;
    struct timeval clockOffset;

    // 16 bit per sample. Assume 44,1 kHz
    // Let's buffer for 3 seconds. Use this like a ringbuffer
    #define FRAME_BUFFER_SIZE 44100 * 3
    uint16_t* frameBuffer;
    uint16_t samples[];
} audiosync_context;

int initSockets(audiosync_context *ctx) {
    if ((ctx->controlFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        debugLog("cannot create control socket");
        return -1;
    }
    if (bind(fd, host->ai_addr, host->ai_addrlen) < 0) {
        debugLog("bind failed");
        return -1;
    }
    if ((ctx->dataFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        debugLog("cannot create control socket");
        return -1;
    }
    if (bind(fd, host->ai_addr, host->ai_addrlen) < 0) {
        debugLog("bind failed");
        return -1;
    }
}

int ResolveAddr(char *host, int port, struct sockaddr *ai_addr) {
    struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; // ipv4 please
	hints.ai_socktype = SOCK_DGRAM; // request TCP
    struct addrinfo *host = 0;
    char portStr[6];
    sprintf(portStr, "%u",port);
	if (getaddrinfo(server, portStr, &hints, &host) < 0) // resolve hostname
	    return -1;

	*ai_addr = (struct sockaddr_in*)memcpy(malloc(src_addr_len), host->ai_addr, host->ai_addrlen); (;
	return 0;
}

void audiosync_addClient(audiosync_context *ctx, const char* host) {
    if (ResolveAddr(host, SNTP_PORT, ctx->clientHosts + ctx->numClients) < 0) {
        debugLog("Could not resolve client ip");
        return;
    }
    ctx->numClients++;
}

typedef struct {

} ssad;

void _controlNetworkRoutine(void *ctxPtr) {
    audiosync_context *ctx = (audiosync_context *)ctxPtr;

    uint8_t buffer[BUFSIZE];
    while(ct) {
        struct sockaddr_in src_addr;
        socklen_t src_addr_len = sizeof(src_addr);

        ssize_t recvlen = recvfrom(ctx->controlFd, buffer, BUFSIZE, MSG_DONTWAIT, (struct sockaddr *)&src_addr, &src_addr_len);
        if (recvlen > 0) {
            // TODO figure out the client offsets
        } else if (recvlen == -1 && ()) {
           if (errno != EAGAIN && errno != EWOULDBLOCK) {
               debugLog("Error while receiving");
               break;
           }
        }

        pthread_sleep(10);// Sleep 10ms
    }
    return NULL;
}

// Packet format: [2 bytes, sequence number][]

void _dataNetworkRoutine(void *ctxPtr) {
    audiosync_context *ctx = (audiosync_context *)ctxPtr;

    uint8_t buffer[BUFSIZE];
    while(ct) {
        struct sockaddr_in src_addr;
        socklen_t src_addr_len = sizeof(src_addr);

        if (ctx->isMaster) {
            // TODO figure out the client offsets

        } else {
            ssize_t recvlen = recvfrom(ctx->dataFd, buffer, BUFSIZE, MSG_DONTWAIT, (struct sockaddr *)&src_addr, &src_addr_len);
            if (recvlen == -1) {
                debugLog("Error while receiving");
                break;
            } else if (recvlen < sizeof()) continue;

            u_short seq_num = ntohs(buf[0] << 8 | buf[1]);

            // TODO enqueue buffer
        }
    }
    return NULL;
}

void _serveNTPRoutine(void *ctxPtr) {
    audiosync_context *ctx = (audiosync_context *)ctxPtr;
    if (msntp_start_server(SNTP_PORT) != 0) return NULL;

    printf("Listening for SNTP clients on port %d...", SNTP_PORT);
    while (ctx->isRunning) {
    int ret = msntp_serve();
    if (ret > 0 || ret < -1)
      return NULL;
    }

    msntp_stop_server();
}

void _setupStreamingThreads(audiosync_context *ctx) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&(ctx->controlThread), &attr, _controlNetworkRoutine, ctx);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&(ctx->controlThread), &attr, _dataNetworkRoutine, ctx);
}

void audiosync_startSending(audiosync_context*) {
    ctx->isRunning = true;
    _setupStreamingThreads(ctx);
    pthread_create(&(ctx->ntpThread), NULL, _serveNTPRoutine, ctx);
}

void audiosync_startReceiving(audiosync_context *ctx, const char* host, audio_utils_fifo *fifo) {
    ctx->isRunning = true;
    ctx->dataFifo = fifo;

    _setupStreamingThreads(ctx);
}

void audiosync_stop(audiosync_context *ctx) {
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

