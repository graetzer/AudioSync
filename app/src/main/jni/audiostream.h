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

#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <pthread.h>
#include "jrtplib/rtpsession.h"

class AudioStreamSession : public jrtplib::RTPSession {
public:
    virtual ~AudioStreamSession() {
        log("Deallocating AudioStream");
        if (format) AMediaFormat_delete(format);
    }
    virtual void Stop() {
        if (isRunning) {
            isRunning = false;// Should kill them all

            if (networkThread && pthread_join(networkThread, NULL)) {
                //perror("Error joining thread");
            }
            networkThread = 0;
            if (ntpThread && pthread_join(ntpThread, NULL)) {
                //perror("Error joining thread");
            }
            ntpThread = 0;
        }
    };

    bool IsRunning() {
        return isRunning;
    }

    pthread_t networkThread = 0, ntpThread = 0;
protected:
    bool isRunning = true;
    AMediaFormat *format;

    void log(const char* logStr, ...);
};

// Initializer functions
AudioStreamSession *audiostream_startStreaming(uint16_t portbase, AMediaExtractor *extractor);
// TODO interrupted callback
AudioStreamSession * audiostream_startReceiving(const char *host, uint16_t portbase);

#endif