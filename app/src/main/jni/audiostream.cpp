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
#include <pthread.h>
#include <android/log.h>

#include "libmsntp/libmsntp.h"
#include "jrtplib/rtpsession.h"
#include "jrtplib/rtppacket.h"
#include "jrtplib/rtpudpv4transmitter.h"
#include "jrtplib/rtpipv4address.h"
#include "jrtplib/rtpsessionparams.h"

#include "audiostream.h"
#include "decoder.h"
#include "audioplayer.h"


using namespace jrtplib;

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioSync", __VA_ARGS__)
#define SNTP_PORT 32442
#define RTP_PORT 32443
#define RTCP_PORT 32444



void audiostream_init() {
    /*ortp_init();
    ortp_scheduler_init();
    ortp_set_log_level_mask(ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR);*/

}

void audiostream_deinit() {
    //ortp_exit();
};

void* _serveNTPServer(void *ctxPtr) {
    audiosync_context *ctx = (audiosync_context *)ctxPtr;

    if (msntp_start_server(SNTP_PORT) != 0)
        return NULL;

    printf("Listening for SNTP clients on port %d...", SNTP_PORT);
    while (ctx->isRunning) {
        int ret = msntp_serve();
        if (ret > 0 || ret < -1) return NULL;
    }

    msntp_stop_server();
    return NULL;
}

static void _checkerror(int rtperr) {
    if (rtperr < 0) {
        debugLog("RTP Error %s", RTPGetErrorString(rtperr).c_str());
        exit(-1);
    }
}

void* _sendStream(void *ctxPtr) {
    audiosync_context *ctx = (audiosync_context *)ctxPtr;

    RTPSessionParams sessparams;
    // IMPORTANT: The local timestamp unit MUST be set, otherwise
    //            RTCP Sender Report info will be calculated wrong
    // In this case, we'll be sending 10 samples each second, so we'll
    // put the timestamp unit to (1.0/10.0)
    sessparams.SetOwnTimestampUnit(1.0/1000.0);// Simon: AMediaExtractor uses microseconds

    //sessparams.SetAcceptOwnPackets(true);
    uint16_t portbase = RTP_PORT;
    RTPUDPv4TransmissionParams transparams;
    transparams.SetPortbase(portbase);
    RTPSession sess;
    int status = sess.Create(sessparams,&transparams);
    _checkerror(status);
    sess.SetDefaultMark(false);

    in_addr_t destip = inet_addr("1.2.3.4");
    uint16_t destport = RTP_PORT;
    RTPIPv4Address addr(destip, destport);
    status = sess.AddDestination(addr);
    _checkerror(status);

    ssize_t written = 0;
    int64_t lastTime = 0;
    while(written > 0 && ctx->isRunning) {
        bool mark = false;
        int64_t time = 0;
        uint8_t buffer[1024];
        written = decoder_extractData(ctx->extractor, buffer, sizeof(buffer), &time);
        uint32_t timestampinc = (uint32_t) time - lastTime;// Assuming it will fit
        lastTime = time;
        if (written < 0) {
            debugLog("Sender: End of stream");
            buffer[0] = '\0';
            status = sess.SendPacket(buffer, 1, 0, true, timestampinc);
        } else {
            status = sess.SendPacket(buffer, written, 0, false, timestampinc);
        }
        _checkerror(status);
        // Not really necessary
        sess.BeginDataAccess();
        // check incoming packets TODO use example4.cpp to add destinations
        if (sess.GotoFirstSourceWithData()) {
            do {
                RTPPacket *pack;
                while ((pack = sess.GetNextPacket()) != NULL) {
                    printf("The sender should not get packets !\n");
                    sess.DeletePacket(pack);
                }
            } while (sess.GotoNextSourceWithData());
        }
        sess.EndDataAccess();
// Without threads we have to do that for ourselves
#ifndef RTP_SUPPORT_THREAD
        status = sess.Poll();
        checkerror(status);
#endif // RTP_SUPPORT_THREAD

        RTPTime::Wait(RTPTime(0,100));
    }

    sess.BYEDestroy(RTPTime(2,0),0,0);

    /*RtpSession *session=rtp_session_new(RTP_SESSION_SENDONLY);
    rtp_session_set_scheduling_mode(session, true);
    rtp_session_set_blocking_mode(session, true);
    rtp_session_set_connected_mode(session, TRUE);
    const char *remoteAddr = "1.2.3.4";
    int remotePort = 1337;
    rtp_session_set_remote_addr(session, remoteAddr, remotePort);
    rtp_session_set_payload_type(session,0);

    //TODO maybe use defined Synchronization Source (SSRC)
    //char *ssrc=getenv("SSRC");
    //if (ssrc!=NULL) {
    //    printf("using SSRC=%i.\n",atoi(ssrc));
    //    rtp_session_set_ssrc(session,atoi(ssrc));
    //}
    size_t size = 160;
    size_t offset = 0;
    uint32_t user_ts = 0;
    while(offset+size < ctx->bufferLength && ctx->isRunning) {
        rtp_session_send_with_ts(session, ctx->buffer, size, user_ts);
        user_ts += size;// Timestamp ?!!
    }

    rtp_session_destroy(session);*/

    return NULL;
}

// Packet format: [2 bytes, sequence number][]
/*void ssrc_cb(RtpSession *session) {
    debugLog("hey, the ssrc has changed !");
}*/


void* _receiveStream(void *ctxPtr) {
    audiosync_context *ctx = (audiosync_context *)ctxPtr;

    RTPSession sess;
    RTPUDPv4TransmissionParams transparams;
    RTPSessionParams sessparams;

    // IMPORTANT: The local timestamp unit MUST be set, otherwise
    //            RTCP Sender Report info will be calculated wrong
    // In this case, we'll be sending 10 samples each second, so we'll
    // put the timestamp unit to (1.0/10.0)
    sessparams.SetOwnTimestampUnit(1.0/10.0);

    sessparams.SetAcceptOwnPackets(true);
    uint16_t portbase = RTP_PORT;
    transparams.SetPortbase(portbase);
    int status = sess.Create(sessparams,&transparams);
    _checkerror(status);

    in_addr_t destip = inet_addr("1.2.3.4");
    uint16_t destport = RTP_PORT;
    RTPIPv4Address addr(destip, destport);

    status = sess.AddDestination(addr);
    _checkerror(status);

    // TODO figure out how to check for connections

    size_t bufferLength = 1024*1024, bufferOffset = 0;
    uint8_t *pcmBuffer = (uint8_t *) malloc(bufferLength);

    AMediaFormat *format = AMediaFormat_new();
    // TODO figure this out
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mpeg");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, 44100);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);

    AMediaCodec *codec = AMediaCodec_createDecoderByType("audio/mpeg");
    status = AMediaCodec_configure(codec, format, NULL, NULL, 0);
    if(status != AMEDIA_OK) return NULL;

    size_t offset = 0;
    bool hasInput = true;
    bool hasOutput = true;
    while(hasInput && ctx->isRunning) {

        sess.BeginDataAccess();
        // check incoming packets
        if (sess.GotoFirstSourceWithData()) {
            do {
                RTPPacket *pack;
                while ((pack = sess.GetNextPacket()) != NULL) {
                    uint8_t * payload = pack->GetPayloadData();
                    size_t length = pack->GetPayloadLength();
                    uint32_t timestamp = pack->GetTimestamp();// TODO figure out if this starts at 0
                    // probably not, so record first timestamp and use differences

                    hasInput = !pack->HasMarker();// We repurposed this as end of stream

                    if (hasInput) {
                        decoder_enqueueBuffer(codec, payload, length, (int64_t)timestamp);
                    } else {
                        debugLog("Receiver: End of stream");
                        // Tell the codec we are done
                        decoder_enqueueBuffer(codec, NULL, -1, (int64_t)timestamp);
                    }
                    hasOutput = decoder_dequeueBuffer(codec, &format, &pcmBuffer, &bufferLength, &bufferOffset);

                    sess.DeletePacket(pack);
                }
            } while (sess.GotoNextSourceWithData());
        }

        sess.EndDataAccess();

#ifndef RTP_SUPPORT_THREAD
        status = sess.Poll();
        checkerror(status);
#endif // RTP_SUPPORT_THREAD
    }

    sess.BYEDestroy(RTPTime(1,0),0,0);

    while(hasOutput) {
        hasOutput = decoder_dequeueBuffer(codec, &format, &pcmBuffer, &bufferLength, &bufferOffset);
    }
    // TODO don't use constants
    audioplayer_startPlayback(pcmBuffer, bufferOffset, 44100, 2);

    /*RtpSession *session=rtp_session_new(RTP_SESSION_RECVONLY);
    rtp_session_set_scheduling_mode(session,1);
    rtp_session_set_blocking_mode(session,1);
    const char *remoteAddr = "1.2.3.4";
    int remotePort = 1337;
    rtp_session_set_local_addr(session,"0.0.0.0", remotePort, -1);// -1 = random rtcp port
    // As far as I understand mean: reject packets from other senders except the first one
    rtp_session_set_connected_mode(session,TRUE);
    rtp_session_set_symmetric_rtp(session, TRUE);// No idea
    rtp_session_enable_adaptive_jitter_compensation(session, true);
    int jittcompMilli = 100;
    rtp_session_set_jitter_compensation(session, jittcompMilli);
    rtp_session_set_payload_type(session,0);
    rtp_session_signal_connect(session,"ssrc_changed",(RtpCallback)ssrc_cb,0);


    uint32_t ts = 0;// Relative timestamp of the package I want
    while(ctx->isRunning) {
        size_t buffSize = 160;
        unsigned char buffer[160];
        int have_more=1;
        while (have_more){
            int outSize = rtp_session_recv_with_ts(session, buffer, buffSize, ts, &have_more);
            // this is to avoid to write to disk some silence before the first RTP packet is returned
            if (outSize > 0) {
                // TODO put this buffer somewhere
            }
        }
        ts += buffSize;
        //ortp_message("Receiving packet.");
    }

    rtp_session_destroy(session);*/

    return NULL;
}

void audiosync_startSending(audiosync_context* ctx, AMediaExtractor *extractor) {
    ctx->isRunning = true;
    ctx->extractor = extractor;
    //pthread_create(&(ctx->ntpThread), NULL, _serveNTPServer, ctx);
    pthread_create(&(ctx->networkThread), NULL, _sendStream, ctx);
}

void audiosync_startReceiving(audiosync_context *ctx) {
    ctx->isRunning = true;

    // TODO try to set priorities on threads
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&(ctx->networkThread), &attr, _receiveStream, ctx);
}

void audiosync_stop(audiosync_context *ctx) {
    if (ctx->isRunning) {
        ctx->isRunning = false;

        if(ctx->networkThread && pthread_join(ctx->networkThread, NULL)) {
            debugLog("Error joining thread");
        }
        ctx->networkThread = NULL;
        if(ctx->ntpThread && pthread_join(ctx->ntpThread, NULL)) {
            debugLog("Error joining thread");
        }
        ctx->ntpThread = NULL;
    }
}

