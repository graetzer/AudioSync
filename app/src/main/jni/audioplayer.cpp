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

#include "readerwriterqueue/readerwriterqueue.h"

using namespace std;

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

// ======== Sync Variables =========
static int64_t current_systemTimeOffsetUs = 0;
static moodycamel::ReaderWriterQueue<audiostream_clockSync> current_syncQueue(1024);
typedef struct {
    size_t frameCount;
    int64_t playbackUSeconds;// The media time derived from the RTP packet timestamps
} _playbackMark;
static moodycamel::ReaderWriterQueue<_playbackMark> current_playbackMarkQueue(1024);

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

    // create output mix, with environmental reverb specified as a non-required interface
    const SLInterfaceID ids[1] = {SL_IID_ENVIRONMENTALREVERB};
    const SLboolean req[1] = {SL_BOOLEAN_FALSE};
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, ids, req);
    _checkerror(result);
    (void) result;

    // realize the output mix
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    _checkerror(result);
}

// Temporary buffer for the audio buffer queue.
// 8192 equals 2048 frames with 2 channels with 16bit PCM format
#define N_BUFFERS 4
#define MAX_BUFFER_SIZE 8192
uint8_t tempBuffers[MAX_BUFFER_SIZE * N_BUFFERS];
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
    size_t frameSize = current_numChannels * sizeof(uint16_t); // 16 bit PCM data
    size_t framesPerBuffer = global_framesPerBuffers * frameSize;
    uint8_t *buf_ptr = tempBuffers + framesPerBuffer * tempBuffers_ix;
    ssize_t frameCount = audio_utils_fifo_read(&fifo, buf_ptr, global_framesPerBuffers);
    if (frameCount > 0) {
        current_playerIsStarving = false;
        current_playerQueuedFrames += frameCount;

        tempBuffers_ix = (tempBuffers_ix + 1) % N_BUFFERS;
        size_t frameCountSize = frameCount * frameSize;
        SLresult result = (*bq)->Enqueue(bq, buf_ptr, (SLuint32) frameCountSize);
        _checkerror(result);
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
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {
            .formatType = SL_DATAFORMAT_PCM,
            .numChannels = numChannels,
            .samplesPerSec = samplesPerSec * 1000,// Milli Hz
            .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
            .containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
            .channelMask = channelMask,
            .endianness = SL_BYTEORDER_LITTLEENDIAN// TODO: compute real endianness
    };
    SLDataSource audioSrc = {&loc_bufq, &format_pcm};

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

void audioplayer_initPlayback(uint32_t samplesPerSec, uint32_t numChannels) {
    debugLog("Audio Sample Rate: %u; Channels: %u", samplesPerSec, numChannels);

    current_samplesPerSec = samplesPerSec;
    current_numChannels = numChannels;
    // TODO maybe resample, figure out if correct. 
    // https://android.googlesource.com/platform/system/media/+/master/audio_utils/resampler.c
    _createBufferQueueAudioPlayer(current_samplesPerSec, current_numChannels);
    if (global_samplesPerSec != current_samplesPerSec) {
        // Will probably result in "AUDIO_OUTPUT_FLAG_FAST denied by client"
        debugLog("Global:%d != Current: %d", global_samplesPerSec, current_samplesPerSec);
    }

    // Initialize the audio buffer queue
    // TODO figure out how to buffer more data? (maybe realloc buffer) Currently: nanosleep
    size_t frameCount = current_samplesPerSec * 60 * 5;// 5 minutes buffer
    size_t frameSize = current_numChannels * sizeof(uint16_t);
    fifoBuffer = realloc(fifoBuffer, frameCount * frameSize);// Is as good as malloc
    audio_utils_fifo_init(&fifo, frameCount, frameSize, fifoBuffer);

    current_playerIsStarving = true;// Enqueue some zeros at first
    debugLog("Initialized playback");
}

void audioplayer_enqueuePCMFrames(const uint8_t *pcmBuffer, size_t pcmSize,
                                  int64_t playbackTimeUs) {
    static size_t enqueuedFrames = 0;

    size_t frameSize = current_numChannels * sizeof(uint16_t);
    size_t frames = pcmSize / frameSize;// Should always fit, MediaCodec uses interleaved 16 bit PCM
    ssize_t written = audio_utils_fifo_write(&fifo, pcmBuffer, frames);

    enqueuedFrames += frames;
    _playbackMark mark = {.frameCount = enqueuedFrames, .playbackUSeconds = playbackTimeUs};
    current_playbackMarkQueue.enqueue(mark);

    // Test if the buffers are empty
    if (current_playerIsStarving && written > 0) {

        // TODO put buffering back in, if the sender mandated 3 secs are not enough
        // size_t sec3 = frameSize * current_samplesPerSec * 3;
        // if (audio_utils_fifo_available(&fifo) > sec3) {
            debugLog("initial enqueue, buffer was empty. Gonna enqueue to kickstart playback");

            _bqPlayerCallback(playerBufferQueue, NULL);
            _bqPlayerCallback(playerBufferQueue, NULL);
        //}
        return;
    } else if (!current_playerIsStarving && written == 0 && pcmSize > 0 && current_isPlaying) {
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

void audioplayer_syncPlayback(int64_t playbackTimeUs, int64_t systemTimeUs) {
    audiostream_clockSync align;
    align.playbackUSeconds = playbackTimeUs;
    align.systemTimeUs = systemTimeUs;
    current_syncQueue.enqueue(align);
    // Process this
    audioplayer_monitorPlayback();
}

void audioplayer_setSystemTimeOffset(int64_t offsetUs) {
    current_systemTimeOffsetUs = offsetUs;
}

void audioplayer_monitorPlayback() {
    if (current_playerIsStarving) return;

    static _playbackMark mark;
    size_t playedFrames = current_playerQueuedFrames;
    if (mark.frameCount < playedFrames) {
        while (current_playbackMarkQueue.peek() != NULL
               && current_playbackMarkQueue.peek()->frameCount < playedFrames) {
            if (!current_playbackMarkQueue.try_dequeue(mark)) break;
        }
    }
    // (1000000 * current_playerQueuedFrames) / current_samplesPerSec;
    // Since we won't call this at the right moment, adjust the actual playback time
    int64_t playbackUSecs = mark.playbackUSeconds
                            + (SECOND_MICRO*(playedFrames - mark.frameCount))/current_samplesPerSec;

    static audiostream_clockSync sync;
    if (sync.playbackUSeconds < playbackUSecs) {
        while (current_syncQueue.peek() != NULL
               && current_syncQueue.peek()->playbackUSeconds < playbackUSecs) {
            if (!current_syncQueue.try_dequeue(sync)) break;
        }
    }

    if (current_isPlaying) {
        static int64_t lastMono = 0;
        static _playbackMark lastMark;
        if (lastMark.frameCount == 0) {
            lastMark = mark;
            lastMono = audiosync_monotonicTimeUs();
            return;
        }

        if (playedFrames - lastMark.frameCount > 30 * current_samplesPerSec / SECOND_MICRO) {
            int64_t monoNow = audiosync_monotonicTimeUs();
            int64_t localInterval = monoNow - lastMono;
            // - mark.playbackUSeconds
            int64_t referenceInterval = lastMark.playbackUSeconds - playbackUSecs;
            double skew = (double)localInterval / (double)referenceInterval;

            debugLog("Skew %f", skew);
            // TODO somehow maary this with the playback time

            lastMark = mark;
            lastMono = monoNow;
        }

    } else {
        int64_t nowUs = audiosync_systemTimeUs() + current_systemTimeOffsetUs;
        int64_t thenUs = sync.systemTimeUs + current_systemTimeOffsetUs;
        // Call start at the right time
        // Let's start when we meet the 5ms threshold
        if (thenUs - nowUs < 5000) {
            /*int64_t playDiff = sync.playbackUSeconds - playbackUSecs;
            if (playDiff > 0) {// Drop frames, you're late
                debugLog("We drop initial frames, you're late");
                int64_t dropFrames = (playDiff * current_samplesPerSec)/SECOND_MICRO;
                uint8_t buffer[dropFrames];
                audio_utils_fifo_read(&fifo, buffer, (size_t)dropFrames);
                current_playerQueuedFrames += dropFrames;
            }*/

            current_isPlaying = true;
            SLresult result = (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);
            _checkerror(result);
            debugLog("Started playback late / early %ld", thenUs - nowUs);
        }
    }
}

void audioplayer_stopPlayback() {
    if (playerPlay) {
        current_isPlaying = false;
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
