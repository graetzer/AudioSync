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
#include <string>
#include <atomic>
#include <android/log.h>

#include "libmsntp/libmsntp.h"
#include "jrtplib/rtpsession.h"
#include "jrtplib/rtppacket.h"
#include "jrtplib/rtpsourcedata.h"
#include "jrtplib/rtpudpv4transmitter.h"
#include "jrtplib/rtpipv4address.h"
#include "jrtplib/rtpipv6address.h"
#include "jrtplib/rtpsessionparams.h"
#include "jrtplib/rtcpapppacket.h"

#include "AudioStreamSession.h"
#include "audioplayer.h"
#include "apppacket.h"
#include "decoder.h"

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioSync", __VA_ARGS__)

using namespace jrtplib;

static void _checkerror(int rtperr) {
    if (rtperr < 0) {
        debugLog("RTP Error: %s", RTPGetErrorString(rtperr).c_str());
        pthread_exit(0);
    }
}

void AudioStreamSession::log(const char *logStr, ...) {
    va_list ap;
    va_start(ap, logStr);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_DEBUG, "AudioStreamSession", logStr, ap);
#else
    vprintf(logStr, ap);
#endif
    va_end(ap);
}