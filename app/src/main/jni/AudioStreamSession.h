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

protected:
    pthread_t networkThread = 0, ntpThread = 0;
    bool isRunning = true;
    AMediaFormat *format;

    void log(const char *logStr, ...);
};

// Initializer functions
//AudioStreamSession *audiostream_startStreaming(uint16_t portbase, AMediaExtractor *extractor);

//AudioStreamSession * audiostream_startReceiving(const char *host, uint16_t portbase);
// TODO An error callback would be nice


#define AUDIOSYNC_SNTP_PORT_OFFSET 3
// IMPORTANT: The local timestamp unit MUST be set, otherwise
//            RTCP Sender Report info will be calculated wrong
//            In this case, we'll be sending 1000 samples each second, so we'll
//            put the timestamp unit to (1.0/1000.0)
// AMediaExtractor uses microseconds too, so we can transport the correct playback time
#define AUDIOSYNC_TIMESTAMP_UNITS (1.0 / 48000.0)

#endif