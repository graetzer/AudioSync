
#ifndef _Included_AudioSync_stream
#define _Included_AudioSync_stream

#include "fifo.h"

typedef struct {
    audiosync_client *clients
    size_t numClients;
} audiosync_context;

typedef struct {
    char *host;
    int streamPort;
    int controlPort;
} audiosync_client;

typedef struct {
    char *host;
    int sntpPort;
    int streamPort;
    int controlPort;
} audiosync_server;

void audiosync_addClient(audiosync_context*, const audiosync_client* client);
void audiosync_startSending(audiosync_context*);
void audiosync_startReceiving(audiosync_context*, const audiosync_server*, void (*)(timeval));

##endif