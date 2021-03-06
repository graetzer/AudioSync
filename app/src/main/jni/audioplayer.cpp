/*
 * audioplayer.c: Encapsulate the OpenSL interface into something simpler to use
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */


#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <cinttypes>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/log.h>

#include "audioplayer.h"
#include "audioutils/fifo.h"
#include "apppacket.h"

#include "readerwriterqueue/readerwriterqueue.h"
//#include "fresample/fresample.h"


#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioPlayer", __VA_ARGS__)

// ========= OpenSL ES =========
// engine interfaces
static SLObjectItf engineObject = NULL;
static SLEngineItf engineEngine;
// output mix interfaces
static SLObjectItf outputMixObject = NULL;
// buffer queue player interfaces
static SLObjectItf playerObject = NULL;
static SLPlayItf playerPlay;
static SLAndroidSimpleBufferQueueItf playerBufferQueue;
static SLPlaybackRateItf playerPlayRate;

//static SLVolumeItf playerVolume;

// ========= Audio Params =========
// Device parameters for playback
static uint32_t global_samplesPerSec;
static uint32_t global_framesPerBuffers;
// Parameters for current audio stream
static uint32_t current_samplesPerSec = 44100;
static uint32_t current_numChannels = 1;
// Will be adjusted over time
static int32_t current_ratePermille = 1000;

// ========= Audio Data Queue =========
// Audio data queue
static struct audio_utils_fifo fifo;
static void *fifoBuffer = NULL;

// ======== Sync Variables =========
static int64_t current_syncSystemTimeUs = 0;
static size_t current_bufferedFrames = 0;

// ======== Timeing offset vars =======
static int64_t current_systemTimeOffsetUs = 0;
static int64_t current_deviceLatency = 0;

// ============ Some info to interact with the callback ==============
typedef struct {
    size_t frameCount;
    int64_t playbackTimeUs;// The media time derived from the RTP packet timestamps
} _playbackMark;
static moodycamel::ReaderWriterQueue<_playbackMark> current_playbackMarkQueue(8192);
// write / read to aligned 32bit integer is atomic
// Written to by monitoring thread
static bool current_isPlaying = false;
static bool current_enableJumps = false;

// Writtten by the callback
static volatile int64_t current_queuedFrames = 0;// Bytes appended to the audio queue
static _playbackMark last_mark;// Last sync mark
static int64_t current_started = 0;
static int64_t current_drop = 0;
static int64_t current_diff = 0;

// =================== Helpers ===================
static const char *_descriptionForResult(SLresult result) {
    switch (result) {
        case SL_RESULT_SUCCESS:
            return "SUCCESS";
        case SL_RESULT_PRECONDITIONS_VIOLATED:
            return "PRECONDITIONS_VIOLATED";
        case SL_RESULT_PARAMETER_INVALID:
            return "PARAMETER_INVALID";
        case SL_RESULT_MEMORY_FAILURE:
            return "MEMORY_FAILURE";
        case SL_RESULT_RESOURCE_ERROR:
            return "RESOURCE_ERROR";
        case SL_RESULT_RESOURCE_LOST:
            return "RESOURCE_LOST";
        case SL_RESULT_IO_ERROR:
            return "IO_ERROR";
        case SL_RESULT_BUFFER_INSUFFICIENT:
            return "BUFFER_INSUFFICIENT";
        case SL_RESULT_CONTENT_CORRUPTED:
            return "CONTENT_CORRUPTED";
        case SL_RESULT_CONTENT_UNSUPPORTED:
            return "CONTENT_UNSUPPORTED";
        case SL_RESULT_CONTENT_NOT_FOUND:
            return "CONTENT_NOT_FOUND";
        case SL_RESULT_PERMISSION_DENIED:
            return "PERMISSION_DENIED";
        case SL_RESULT_FEATURE_UNSUPPORTED:
            return "FEATURE_UNSUPPORTED";
        case SL_RESULT_INTERNAL_ERROR:
            return "INTERNAL_ERROR";
        case SL_RESULT_OPERATION_ABORTED:
            return "OPERATION_ABORTED";
        case SL_RESULT_CONTROL_LOST:
            return "CONTROL_LOST";
        default:
            return "Unknown error code";
    }
}

#define _checkerror(result) _checkerror_internal(result, __FUNCTION__)

static void _checkerror_internal(SLresult result, const char *f) {
    if (SL_RESULT_SUCCESS != result) {
        debugLog("%s - OpenSL ES error: %s", f, _descriptionForResult(result));
        // TODO figure out which errors to treat as fatal
        if (result != SL_RESULT_BUFFER_INSUFFICIENT) {
            exit(-1);
        }
    }
}

// =================== Setup OpenSL objects ===================

// create the engine and output mix objects
static void _createEngine() {
    // create engine
    SLresult result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    _checkerror(result);

    // realize the engine
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    _checkerror(result);

    // get the engine interface, which is needed in order to create other objects
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    _checkerror(result);

    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, NULL, NULL);
    _checkerror(result);
    (void) result;

    // realize the output mix
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    _checkerror(result);
}

// Temporary buffer for the audio buffer queue.
// 8192 equals 2048 frames with 2 channels with 16bit PCM format
#define N_BUFFERS 3
#define MAX_BUFFER_SIZE (8192)
int16_t tempBuffers[MAX_BUFFER_SIZE * N_BUFFERS];
uint32_t tempBuffers_ix = 0;
// this callback handler is called every time a buffer finishes playing
static void _bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context __unused) {
    // Assuming PCM 16
    // Let's try to fill in the ideal amount of frames. Frame size: numChannels * sizeof(int16_t)
    // global_framesPerBuffers is the max amount of frames we read
    size_t frameSize = current_numChannels * sizeof(int16_t);
    size_t maxBufferSize = global_framesPerBuffers * frameSize;

    tempBuffers_ix = (tempBuffers_ix + 1) % N_BUFFERS;
    int16_t *buf_ptr = tempBuffers + maxBufferSize * tempBuffers_ix;


    static int64_t accuracy;
    int64_t nowUs = audiosync_coarseTimeUs() + current_systemTimeOffsetUs + current_deviceLatency;
    if (!current_isPlaying && current_syncSystemTimeUs != 0) {
        if (accuracy == 0) accuracy = audiosync_coarseAccuracyUs();// Usually 10ms

        int64_t diff = current_syncSystemTimeUs - nowUs;
        current_isPlaying = diff < accuracy;
        current_started = nowUs;
        if (diff < -accuracy) {
            int64_t drop = (-diff * current_samplesPerSec) / SECOND_MICRO;

            do {
                size_t requestFrames = (size_t)drop;
                if (requestFrames*frameSize > MAX_BUFFER_SIZE)
                    requestFrames = MAX_BUFFER_SIZE/frameSize;

                ssize_t frameCount = audio_utils_fifo_read(&fifo, buf_ptr, requestFrames);
                if (frameCount <= 0) break;
                current_queuedFrames += frameCount;
                drop -= frameCount;
            } while(drop > 0);
        }
    }

    if (current_isPlaying) {

        if (last_mark.frameCount <= current_queuedFrames) {
            while (current_playbackMarkQueue.peek() != NULL
                   && current_playbackMarkQueue.peek()->frameCount <= current_queuedFrames) {
                if(!current_playbackMarkQueue.try_dequeue(last_mark)) break;
            }
        }

        // Since we won't call this at the exact right moment, adjust the actual playback time
        int64_t correction = (SECOND_MICRO*(current_queuedFrames
                                            - last_mark.frameCount))/current_samplesPerSec;
        int64_t playbackTimeUs = last_mark.playbackTimeUs + correction;
        int64_t systemTimeUs = current_syncSystemTimeUs + playbackTimeUs;

        int64_t diff = nowUs - systemTimeUs;
        int64_t drop = (diff * current_samplesPerSec) / SECOND_MICRO;
        current_drop = drop;
        current_diff = diff;

        size_t requestFrames = global_framesPerBuffers;
        if (current_enableJumps) {
            if (diff >= accuracy/2) {// Speed up playback a bit
                requestFrames += drop;
                if (requestFrames * frameSize > MAX_BUFFER_SIZE) {
                    requestFrames = MAX_BUFFER_SIZE / frameSize;
                    drop = requestFrames - global_framesPerBuffers;
                }
            } else if (diff <= -accuracy/2) {// Play silence
                drop = -drop;
                if (drop < global_framesPerBuffers) {
                    size_t ss = (size_t) drop * current_numChannels;
                    ssize_t frameCount = audio_utils_fifo_read(&fifo, buf_ptr + ss,
                                                               requestFrames - drop);
                    if (frameCount > 0) {
                        memset(buf_ptr, 0, ss);

                        SLresult result = (*bq)->Enqueue(bq, buf_ptr, (SLuint32) maxBufferSize);
                        _checkerror(result);
                        current_queuedFrames += frameCount;
                        return;
                    }
                }
                memset(buf_ptr, 1, maxBufferSize);// for some reason 0 doesn't work
                SLresult result = (*bq)->Enqueue(bq, buf_ptr, (SLuint32) maxBufferSize);
                _checkerror(result);
                return;
            }
        }

        // Now we can start playing some sound
        ssize_t frameCount = audio_utils_fifo_read(&fifo, buf_ptr, requestFrames);
        if (frameCount > 0) {

            // Implementing dropping or stretching of frames
            if (current_enableJumps && diff >= accuracy/2 && frameCount == requestFrames) {
                int mod = (int)(frameCount / drop);
                int nFrame = 0;
                for (int frame = 0; frame < frameCount; frame++) {
                    int from = frame * current_numChannels;
                    int to = nFrame * current_numChannels;

                    if (drop > 0 && frame % mod == 0) {
                        int next = from + current_numChannels;
                        for (int c = 0; c < current_numChannels; c++)
                            buf_ptr[to+c] = (int16_t) ((buf_ptr[from+c] + buf_ptr[next+c]) / 2);
                        drop--;
                        continue;
                    }

                    for (int c = 0; c < current_numChannels; c++) buf_ptr[to+c] = buf_ptr[from+c];
                    nFrame++;
                }
                current_queuedFrames += frameCount - global_framesPerBuffers;
                frameCount = global_framesPerBuffers;
            }

            current_queuedFrames += frameCount;
            maxBufferSize = frameCount * current_numChannels * sizeof(int16_t);
            SLresult result = (*bq)->Enqueue(bq, buf_ptr, (SLuint32) maxBufferSize);
            _checkerror(result);

            return;
        }
        debugLog("FIFO buffer is empty");
    } else {
        // Don't actually starve the buffer, just keep it running
        memset(buf_ptr, 1, maxBufferSize);// for some reason 0 doesn't work
        //_generateTone(buf_ptr);
        SLresult result = (*bq)->Enqueue(bq, buf_ptr, (SLuint32) maxBufferSize);
        _checkerror(result);
    }
}

// create buffer queue audio player
static void _createBufferQueueAudioPlayer(SLuint32 samplesPerSec, SLuint32 numChannels) {
    SLresult result;
    SLuint32 channelMask = SL_SPEAKER_FRONT_CENTER;
    if (numChannels >= 2) {
        numChannels = 2;// I don't think devices support more than 2
        channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
        debugLog("Using stereo audio");
    }

    // configure audio source
    SLDataLocator_AndroidSimpleBufferQueue bufferQueue = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 1};
    SLDataFormat_PCM format_pcm = {
            .formatType = SL_DATAFORMAT_PCM,
            .numChannels = numChannels,
            .samplesPerSec = samplesPerSec * 1000,// Milli Hz
            .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
            .containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
            .channelMask = channelMask,
            .endianness = SL_BYTEORDER_LITTLEENDIAN// TODO: compute real endianness
    };
    SLDataSource audioSrc = {&bufferQueue, &format_pcm};

    // configure audio sink
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // create audio player
    const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_PLAYBACKRATE};//, SL_IID_VOLUME
    const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &playerObject, &audioSrc, &audioSnk,
                                                2, ids, req);// ids length, interfaces, required
    _checkerror(result);

    // realize the player
    result = (*playerObject)->Realize(playerObject, SL_BOOLEAN_FALSE);
    _checkerror(result);

    // get the play interface
    result = (*playerObject)->GetInterface(playerObject, SL_IID_PLAY, &playerPlay);
    _checkerror(result);

    // get the buffer queue interface
    result = (*playerObject)->GetInterface(playerObject, SL_IID_BUFFERQUEUE, &playerBufferQueue);
    _checkerror(result);

    // get playback rate interface
    result = (*playerObject)->GetInterface(playerObject, SL_IID_PLAYBACKRATE, &playerPlayRate);
    _checkerror(result);

    // register callback on the buffer queue
    result = (*playerBufferQueue)->RegisterCallback(playerBufferQueue, _bqPlayerCallback, NULL);
    _checkerror(result);

    // get the volume interface
    //result = (*playerObject)->GetInterface(playerObject, SL_IID_VOLUME, &playerVolume);
    //_checkerror(result);
}

void _cleanupBufferQueueAudioPlayer() {
    // destroy buffer queue audio player object, and invalidate all associated interfaces
    if (playerObject != NULL) {
        (*playerObject)->Destroy(playerObject);
        playerObject = NULL;
        playerPlay = NULL;
        playerBufferQueue = NULL;
        //playerPlayRate = NULL;
        //playerVolume = NULL;
    }
}

// =================== Public API calls ===================

void audioplayer_initGlobal(uint32_t samplesPerSec, uint32_t framesPerBuffer) {
    debugLog("Device Buffer Size: %d ;  Sample Rate: %d", framesPerBuffer, samplesPerSec);
    global_samplesPerSec = samplesPerSec;
    global_framesPerBuffers = framesPerBuffer;

    if (framesPerBuffer > MAX_BUFFER_SIZE) {
        debugLog("ATTENTION: Global buffersize is bigger than MAX_BUFFER_SIZE");
    }
}

void audioplayer_initPlayback(uint32_t samplesPerSec, uint32_t numChannels) {
    if (engineObject == NULL) {
        _createEngine();
        debugLog("Initialized AudioPlayer");
    }

    debugLog("Audio Sample Rate: %u; Channels: %u", samplesPerSec, numChannels);
    // Reset our entire state
    current_samplesPerSec = samplesPerSec;
    current_numChannels = numChannels;
    current_ratePermille = 1000;
    current_bufferedFrames = 0;
    current_syncSystemTimeUs = 0;
    current_isPlaying = false;
    current_enableJumps = false;
    current_queuedFrames = 0;
    memset(&last_mark, 0, sizeof(_playbackMark));
    current_started = 0;
    current_drop = 0;
    current_diff = 0;

    // Empty queues
    if (current_playbackMarkQueue.size_approx() > 0) {
        bool cont = true;
        while(cont) {
            cont = current_playbackMarkQueue.pop();
        }
    }

    _createBufferQueueAudioPlayer(current_samplesPerSec, current_numChannels);
    // Always use optimal rate, resample by slowing down playback
    if (global_samplesPerSec != samplesPerSec) {
        // Will probably result in "AUDIO_OUTPUT_FLAG_FAST denied by client"
        debugLog("Global:%d != Current: %d", global_samplesPerSec, samplesPerSec);
        (*playerPlayRate)->SetRate(playerPlayRate, (SLpermille) 1000);
    }

    // Initialize the audio buffer queue
    size_t frameCount = current_samplesPerSec * 60 * 5;// 5 minutes buffer
    size_t frameSize = current_numChannels * sizeof(uint16_t);
    fifoBuffer = realloc(fifoBuffer, frameCount * frameSize);// Is as good as malloc
    audio_utils_fifo_init(&fifo, frameCount, frameSize, fifoBuffer);

    // This call has latency, we need to keep the audio system starving to perform start / pause
    SLresult result = (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);
    _checkerror(result);
    _bqPlayerCallback(playerBufferQueue, NULL);
    debugLog("Initialized playback");
}

void audioplayer_enqueuePCMFrames(const uint8_t *pcmBuffer, size_t pcmSize, int64_t playbackTimeUs) {


    size_t frameSize = current_numChannels * sizeof(uint16_t);
    size_t frames = pcmSize / frameSize;// Should always fit, MediaCodec uses interleaved 16 bit PCM
    ssize_t written = audio_utils_fifo_write(&fifo, pcmBuffer, frames);
    // Note: playbackTimeUs corresponds to the end of the sample, not the start.
    current_bufferedFrames += frames;

    if (current_syncSystemTimeUs == 0) {
        debugLog("WTF");// what a terible failure
    }
    _playbackMark mark;
    mark.playbackTimeUs = playbackTimeUs;
    mark.frameCount = current_bufferedFrames;
    current_playbackMarkQueue.enqueue(mark);
    //debugLog("Enqueued: %" PRId64 ", pl: %" PRId64, mark.systemTimeUs, playbackTimeUs);

    if (written == 0 && pcmSize > 0 && current_isPlaying) {
        audioplayer_monitorPlayback();
        debugLog("FiFo queue seems to be full, slowing down");
        struct timespec req;
        req.tv_sec = (time_t) 3;// Let's sleep for a while
        req.tv_nsec = 0;
        nanosleep(&req, NULL);
        audioplayer_enqueuePCMFrames(pcmBuffer, pcmSize, playbackTimeUs);
    }
    audioplayer_monitorPlayback();
}

void audioplayer_syncPlayback(int64_t systemTimeUs, int64_t playbackTimeUs) {
    if (current_syncSystemTimeUs == 0) {
        /*_playbackMark mark;
        mark.playbackTimeUs = 0;
        mark.frameCount = 0;
        current_playbackMarkQueue.enqueue(mark);*/
        current_syncSystemTimeUs = systemTimeUs - playbackTimeUs;

        int64_t nowUs = audiosync_systemTimeUs() + current_systemTimeOffsetUs;
        int64_t diff = systemTimeUs - nowUs;
        debugLog("Start determined to: %" PRId64, current_syncSystemTimeUs);
        debugLog("Starting playback in %fs", diff / 1E6);
    }

    //debugLog("Adding Sync: System time %"PRId64". PlaybackTime: %"PRId64, systemTimeUs, playbackTimeUs);
    audioplayer_monitorPlayback();
}

void audioplayer_setSystemTimeOffset(int64_t offsetUs) {
    current_systemTimeOffsetUs = offsetUs;
    debugLog("NTP offset %" PRId64, offsetUs);
}

void audioplayer_setDeviceLatency(int64_t latencyUs) {
    current_deviceLatency = latencyUs;
    debugLog("Set device latency to" PRId64, latencyUs);
}

void audioplayer_monitorPlayback() {

    //debugLog("Mark: Sample %lu Presentation Time: %"PRId64, last_mark.frameCount, last_mark.playbackTimeUs);
    //debugLog("Sync: System time %"PRId64". Presentation Time: %"PRId64, last_sync.systemTimeUs, last_sync.playbackTimeUs);

    static int64_t lastPrint;
    static int64_t lastDiff;
    static int64_t lastAdjustment;
    static int64_t listenTime;

    if (current_isPlaying) {
        int64_t nowUs = audiosync_monotonicTimeUs();
        if (lastPrint == 0) {
            int64_t diff = current_syncSystemTimeUs - current_started;
            debugLog("Started late / early %fs. Diff in callback %fs", diff/1E6, current_diff/1E6);
            debugLog("Clock accuracy %" PRId64, audiosync_coarseAccuracyUs());
            lastPrint = nowUs;
            lastDiff = current_diff;
            lastAdjustment = nowUs;// don't wait full 2 seconds
            listenTime = 2*SECOND_MICRO;
            return;
        }

        int64_t wait = nowUs - lastPrint;
        const int64_t diff = current_diff;
        if (wait > 5*SECOND_MICRO) {
            // Is positive if we are late, negative if we are too fast
            debugLog("Accumulated diff: %fs", diff/1E6);
            lastPrint = nowUs;
        }
        wait = nowUs - lastAdjustment;
        if (!current_enableJumps && wait > listenTime) {
            listenTime = 5*SECOND_MICRO;// Don't reset so often

            double vDelta = (double)(lastDiff-diff) / wait;

            int32_t offset = (int32_t)(vDelta * 1000);
            debugLog("Speed: %f. Drop %" PRId64 ". Rate offset %" PRId32, vDelta + 1.0,
                     current_drop, offset);

            int32_t newRate = current_ratePermille - offset/2;
            if (newRate < 500) newRate = 500;
            if (newRate > 2000) newRate = 2000;
            if (2 <= labs((long)offset)) {
                current_ratePermille = newRate;
                (*playerPlayRate)->SetRate(playerPlayRate, (SLpermille) current_ratePermille);
                debugLog("Adjusted Rate");
            }
            current_enableJumps = true;
            lastDiff = diff;
            lastAdjustment = nowUs;
            debugLog("Enabled Jumps");
        } else if (current_enableJumps
                   && wait > llabs((long long)lastDiff) && lastDiff != diff) {// Use lastDiff as the jumping allowed time
            current_enableJumps = false;
            lastDiff = diff;
            lastAdjustment = nowUs;
            debugLog("Disabled Jumps");
        }
    } else  {
        lastPrint = 0;
    }
}

int64_t audioplayer_currentPlaybackTimeUs() {
    return last_mark.playbackTimeUs;
}

void audioplayer_stopPlayback() {
    debugLog("Stopping playback");
    if (playerPlay) {
        SLresult result = (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_STOPPED);
        _checkerror(result);
        debugLog("Stopped playback");
    }

    // Cleanup audio-player so we can use different parameters
    _cleanupBufferQueueAudioPlayer();
    audio_utils_fifo_deinit(&fifo);// noop
}

// shut down the native audio system
void audioplayer_cleanup() {
    audioplayer_stopPlayback();

    // destroy output mix object, and invalidate all associated interfaces
    if (outputMixObject != NULL) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = NULL;
    }
    // destroy engine object, and invalidate all associated interfaces
    if (engineObject != NULL) {
        (*engineObject)->Destroy(engineObject);
        engineObject = NULL;
        engineEngine = NULL;
    }
    // free the buffer if it was allocated
    if (fifoBuffer != NULL) {
        free(fifoBuffer);
        fifoBuffer = NULL;
    }
}
