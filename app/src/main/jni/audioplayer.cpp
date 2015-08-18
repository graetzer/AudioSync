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
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/log.h>

#include "audioplayer.h"
#include "audioutils/fifo.h"
#include "apppacket.h"

#include <queue>
#include <cinttypes>
#include <cmath>

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
static bool current_isPlaying = false;
// Playback rate default
static uint32_t current_defaultRatePromille = 1000;
static uint32_t current_ratePromille = 1000;

// ======== Sync Variables =========
static int64_t current_systemTimeOffsetUs = 0;
static std::queue<audiostream_clockSync> current_syncQueue;
typedef struct {
    size_t frameCount;
    int64_t playbackTimeUs;// The media time derived from the RTP packet timestamps
} _playbackMark;
static std::queue<_playbackMark> current_playbackMarkQueue;

// ========= Audio Data Queue =========
// Audio data queue
static struct audio_utils_fifo fifo;
static void *fifoBuffer = NULL;

// ============ Some info to interact with the callback ==============
// write / read to aligned 32bit integer is atomic, read from network thread, write from callback
static volatile bool current_playerIsStarving = true;
//static volatile int64_t current_playerCallOffsetUs = 0;// Time offset between two player callbacks
static volatile size_t current_playerQueuedFrames = 0;// Bytes appended to the audio queue

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

void _generateTone(int16_t *buf_ptr) {
    static double theta;
    double theta_increment = 2.0 * M_PI * 432.0 / current_samplesPerSec;
    // Generate the samples
    for (int frame = 0; frame < global_framesPerBuffers; frame++) {
        int16_t v = (int16_t) (sin(theta) * 65535.0 * 0.1);

        for (int c = 0; c < current_numChannels; c++) {
            buf_ptr[frame * current_numChannels + c] = (uint8_t) (v & 0x00FF);
        }
        theta += theta_increment;
        if (theta > 2.0 * M_PI) {
            theta -= 2.0 * M_PI;
        }
    }
}

// Temporary buffer for the audio buffer queue.
// 8192 equals 2048 frames with 2 channels with 16bit PCM format
#define N_BUFFERS 3
#define MAX_BUFFER_SIZE 4096
int16_t tempBuffers[MAX_BUFFER_SIZE * N_BUFFERS];
uint32_t tempBuffers_ix = 0;
// this callback handler is called every time a buffer finishes playing
static void _bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    if (fifoBuffer == NULL) {
        debugLog("FifoBuffer should not be null");
        return;
    }

    // Assuming PCM 16
    // Let's try to fill in the ideal amount of frames. Frame size: numChannels * sizeof(int16_t)
    // global_framesPerBuffers is the max amount of frames we read
    size_t frameCountSize = global_framesPerBuffers * current_numChannels * sizeof(int16_t);
    int16_t *buf_ptr = tempBuffers + frameCountSize * tempBuffers_ix;
    tempBuffers_ix = (tempBuffers_ix + 1) % N_BUFFERS;

    if (!current_isPlaying) {// Don't actually starve the buffer, just keep it running
        //memset(buf_ptr, 1, frameCountSize);// for some reason 0 doesn't work
        _generateTone(buf_ptr);
        SLresult result = (*bq)->Enqueue(bq, buf_ptr, (SLuint32) frameCountSize);
        _checkerror(result);
        return;
    }

    // Now we can start playing
    ssize_t frameCount = audio_utils_fifo_read(&fifo, buf_ptr, global_framesPerBuffers);
    if (frameCount > 0) {
        // On first call, merge with standby sound
        if (current_playerQueuedFrames == 0) {
            int16_t *other_ptr = tempBuffers + frameCountSize * tempBuffers_ix;
            _generateTone(other_ptr);
            for (int frame = 0; frame < frameCount; frame++) {
                for (int c = 0; c < current_numChannels; c++) {
                    size_t xx = frame * current_numChannels + c;
                    buf_ptr[xx] = (int16_t)((buf_ptr[xx]*(frameCount - frame)
                                             + other_ptr[xx]*frame) / frameCount);
                }
            }
        }

        frameCountSize = frameCount * current_numChannels * sizeof(int16_t);
        SLresult result = (*bq)->Enqueue(bq, buf_ptr, (SLuint32) frameCountSize);
        _checkerror(result);

        current_playerIsStarving = false;
        current_playerQueuedFrames += frameCount;

    } else {
        current_playerIsStarving = true;
        debugLog("_bqPlayerCallback: Audio FiFo is empty");
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
    SLDataLocator_AndroidSimpleBufferQueue bufferQueue = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                          2};
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
    const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_PLAYBACKRATE};//SL_IID_VOLUME
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
        //bqPlayerEffectSend = NULL;
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

    _createEngine();
    debugLog("Initialized AudioPlayer");
}

// TODO maybe rename these
static _playbackMark mark;
static audiostream_clockSync sync;
void audioplayer_initPlayback(uint32_t samplesPerSec, uint32_t numChannels) {
    debugLog("Audio Sample Rate: %u; Channels: %u", samplesPerSec, numChannels);
    // Reset our entire state
    current_samplesPerSec = global_samplesPerSec;//samplesPerSec;
    current_numChannels = numChannels;
    current_playerIsStarving = true;// Enqueue some zeros at first
    current_playerQueuedFrames = 0;
    current_isPlaying = false;
    current_defaultRatePromille = 1000;
    current_ratePromille = 1000;
    memset(&mark, 0, sizeof(_playbackMark));
    memset(&sync, 0, sizeof(audiostream_clockSync));
    // Empty queues
    if (current_playbackMarkQueue.size() > 0) {
        std::queue<_playbackMark> empty;
        std::swap(current_playbackMarkQueue, empty);
    }
    if (current_syncQueue.size() > 0) {
        std::queue<audiostream_clockSync> empty;
        std::swap(current_syncQueue, empty);
    }

    _createBufferQueueAudioPlayer(current_samplesPerSec, current_numChannels);
    debugLog("TestTest");
    // Always use optimal rate, resample by slowing down playback
    if (global_samplesPerSec != samplesPerSec && playerPlayRate != NULL) {
        // Will probably result in "AUDIO_OUTPUT_FLAG_FAST denied by client"
        debugLog("Global:%d != Current: %d", global_samplesPerSec, samplesPerSec);

        current_defaultRatePromille = (1000 * samplesPerSec)/global_samplesPerSec;
        current_ratePromille = current_defaultRatePromille;
        debugLog("El cheapo resampling, slow down to %" PRId32, current_ratePromille);
        (*playerPlayRate)->SetRate(playerPlayRate, (SLpermille) current_ratePromille);
    }

    // Initialize the audio buffer queue
    // TODO figure out how to buffer more data? (maybe realloc buffer) Currently: nanosleep
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
    static size_t enqueuedFrames;

    size_t frameSize = current_numChannels * sizeof(uint16_t);
    size_t frames = pcmSize / frameSize;// Should always fit, MediaCodec uses interleaved 16 bit PCM
    ssize_t written = audio_utils_fifo_write(&fifo, pcmBuffer, frames);
    enqueuedFrames += frames;

    _playbackMark mark;
    mark.frameCount = enqueuedFrames;
    mark.playbackTimeUs = playbackTimeUs;
    current_playbackMarkQueue.push(mark);
    //debugLog("Enqueued Time: %"PRId64, playbackTimeUs);

//    // Test if the buffers are empty
//    if (current_playerIsStarving && current_isPlaying && written > 0) {
//        //debugLog("initial enqueue, buffer was empty. Gonna enqueue to kickstart playback");
//        _bqPlayerCallback(playerBufferQueue, NULL);
//    } else
    if (!current_playerIsStarving && written == 0 && pcmSize > 0 && current_isPlaying) {
        debugLog("FiFo queue seems to be full, slowing down");
        struct timespec req;
        req.tv_sec = (time_t) 5;// Let's sleep for a while
        req.tv_nsec = 0;
        nanosleep(&req, NULL);
        audioplayer_enqueuePCMFrames(pcmBuffer, pcmSize, playbackTimeUs);
    }
    // This will start the playback
    audioplayer_monitorPlayback();
}

void audioplayer_syncPlayback(int64_t systemTimeUs, int64_t playbackTimeUs) {
    audiostream_clockSync sync;
    sync.systemTimeUs = systemTimeUs;
    sync.playbackTimeUs = playbackTimeUs;
    current_syncQueue.push(sync);
    //debugLog("Adding Sync: System time %"PRId64". PlaybackTime: %"PRId64, systemTimeUs, playbackTimeUs);
    audioplayer_monitorPlayback();
}

void audioplayer_setSystemTimeOffset(int64_t offsetUs) {
    current_systemTimeOffsetUs = offsetUs;
}

void audioplayer_monitorPlayback() {

    size_t playedFrames = current_playerQueuedFrames;

    int64_t nowUs = audiosync_systemTimeUs() + current_systemTimeOffsetUs;
    if (mark.frameCount < playedFrames) {
        while (!current_playbackMarkQueue.empty()
               && current_playbackMarkQueue.front().frameCount <= playedFrames) {
            mark = current_playbackMarkQueue.front();
            current_playbackMarkQueue.pop();
        }
        // Since we won't call this at the exact right moment, adjust the actual playback time
        mark.playbackTimeUs += (SECOND_MICRO*(playedFrames - mark.frameCount))/current_samplesPerSec;
        mark.frameCount = playedFrames;
    }

    if (sync.playbackTimeUs < mark.playbackTimeUs || sync.systemTimeUs == 0) {// == 0 => first call
        while (!current_syncQueue.empty()
               && current_syncQueue.front().playbackTimeUs <= mark.playbackTimeUs) {
            sync = current_syncQueue.front();
            current_syncQueue.pop();
            sync.systemTimeUs += current_systemTimeOffsetUs;
        }
        // Workaround to force waiting, in case we didn't receive a sync packet yet
        if (sync.systemTimeUs == 0) return;

        sync.systemTimeUs += (mark.playbackTimeUs - sync.playbackTimeUs);
        sync.playbackTimeUs = mark.playbackTimeUs;
    }

    //debugLog("Mark: Sample %lu Presentation Time: %"PRId64, mark.frameCount, mark.playbackTimeUs);
    //debugLog("Sync: System time %"PRId64". Presentation Time: %"PRId64, sync.systemTimeUs, sync.playbackTimeUs);
    //debugLog("Actual played: %lu", playedFrames);

    static _playbackMark lastMark;
    static int64_t lastDiff;
    if (current_isPlaying) {

        int64_t waitSec = 10;
        if (playedFrames - lastMark.frameCount > waitSec * current_samplesPerSec) {

            int64_t diff = nowUs - sync.systemTimeUs;
            double skew = (double)(diff-lastDiff) / (waitSec * 1000000.0);
            debugLog("Accumulated diff: %fs, Speed: %f", diff/1E6, skew + 1.0);
            // TODO somehow marry this with the playback time

            lastMark = mark;
            lastDiff = diff;
        }

    } else {
        //debugLog("%"PRId64" - %"PRId64" = %"PRId64, sync.systemTimeUs, nowUs, sync.systemTimeUs - nowUs);
        // Call start at the right time, when we meet the threshold
        // TODO use define, same as RTPTime::Wait in the loop in ReceiverSession::RunNetwork()
        if (sync.systemTimeUs - nowUs < 1000) {
            /*int64_t playDiff = sync.playbackTimeUs - playbackTimeUs;
            if (playDiff > 0) {// Drop frames, you're late
                debugLog("We drop initial frames, you're late");
                int64_t dropFrames = (playDiff * current_samplesPerSec)/SECOND_MICRO;
                uint8_t buffer[dropFrames];
                audio_utils_fifo_read(&fifo, buffer, (size_t)dropFrames);
                current_playerQueuedFrames += dropFrames;
            }*/

            // Initialize measurments properly
            lastMark = mark;
            lastDiff = 0;

            current_isPlaying = true;
            debugLog("Started playback late / early %" PRId64, sync.systemTimeUs - nowUs);
        }
    }
}

int64_t audioplayer_currentPlaybackTimeUs() {
    return mark.playbackTimeUs;
}

void audioplayer_stopPlayback() {
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
