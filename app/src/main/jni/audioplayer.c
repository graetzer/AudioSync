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

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "audioplayer.h"


#include <android/log.h>
#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioPlayer", __VA_ARGS__)


// engine interfaces
 static SLObjectItf engineObject = NULL;
 static SLEngineItf engineEngine;

 // output mix interfaces
 static SLObjectItf outputMixObject = NULL;

 // buffer queue player interfaces
 static SLObjectItf bqPlayerObject = NULL;
 static SLPlayItf bqPlayerPlay;
 static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
 //static SLEffectSendItf bqPlayerEffectSend;
 static SLVolumeItf bqPlayerVolume;

 // Parameters for playback
 static int32_t global_samplesPerSec;
 static int32_t global_framesPerBuffers;

 /*

 #define N_BUFFERS 4
 #define MAX_BUFFER_SIZE 4096
  int16_t temp_buffer[MAX_BUFFER_SIZE * N_BUFFERS];
  uint16_t *sample_fifo_buffer=0;*/

// Current data to play
 static size_t current_temp_buffer_ix = 0;// Number of pages
 static int8_t* temp_buffer;
 static size_t temp_buffer_size;

 // create the engine and output mix objects
 static void _createEngine() {
    // create engine
    SLresult result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    assert(SL_RESULT_SUCCESS == result);

    // realize the engine
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);

    // get the engine interface, which is needed in order to create other objects
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    assert(SL_RESULT_SUCCESS == result);

    // create output mix, with environmental reverb specified as a non-required interface
    const SLInterfaceID ids[1] = {SL_IID_ENVIRONMENTALREVERB};
    const SLboolean req[1] = {SL_BOOLEAN_FALSE};
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, ids, req);
    assert(SL_RESULT_SUCCESS == result);
    (void)result;

     // realize the output mix
     result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
     assert(SL_RESULT_SUCCESS == result);
 }

 // this callback handler is called every time a buffer finishes playing
static void _bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    if (temp_buffer == NULL) return;
    //(*bq)->Enqueue(bqPlayerBufferQueue, temp_buffer, temp_buffer_size);
    //audioplayer_stopPlayback();

     // Assuming PCM 16
     size_t size = global_framesPerBuffers;// Let's try to fill in the ideal amount of frames
     size_t offset = size * current_temp_buffer_ix;
     if (offset < temp_buffer_size) {
        if (offset + size > temp_buffer_size)// if there isn't enough left, cancel
            size = temp_buffer_size - offset;

        SLresult result = (*bq)->Enqueue(bqPlayerBufferQueue, temp_buffer + offset, size);
        if (result != SL_RESULT_SUCCESS) {
            // the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
            // which for this code example would indicate a programming error
            debugLog("Error enqueuing buffer %u", (unsigned int)result);
        }
        current_temp_buffer_ix++;
     } else {
        current_temp_buffer_ix = 0;// Just loop it
        int8_t empty[1] = {0};// If we don't enqueue seomething it breaks
        (*bq)->Enqueue(bqPlayerBufferQueue, &empty, 1);
     }//*/

    /*int16_t *buf_ptr = temp_buffer + global_bufsize * current_temp_buffer_ix;

    ssize_t frameCount = audio_utils_fifo_read(sample_fifo, buf_ptr, global_bufsize);
    if (frameCount > 0) {
    SLresult result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buf_ptr, frameCount * 2);
        assert(SL_RESULT_SUCCESS == result);
        current_temp_buffer_ix = (current_temp_buffer_ix + 1) % N_BUFFERS;
    }*/
 }

 // create buffer queue audio player
 static void _createBufferQueueAudioPlayer(int samplesPerSec) {
     SLresult result;

     // configure audio source
     SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
     SLDataFormat_PCM format_pcm = {
         	.formatType = SL_DATAFORMAT_PCM,
         	.numChannels = 1,
         	.samplesPerSec = samplesPerSec * 1000,// Milli Hz  SL_SAMPLINGRATE_8
         	.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
         	.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
         	.channelMask = SL_SPEAKER_FRONT_CENTER,
         	.endianness = SL_BYTEORDER_LITTLEENDIAN// TODO: compute real endianness
       	};
     SLDataSource audioSrc = {&loc_bufq, &format_pcm};

     // configure audio sink
     SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
     SLDataSink audioSnk = {&loc_outmix, NULL};

     // create audio player
     const SLInterfaceID ids[2] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_VOLUME};
     const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
     result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk, 2, ids, req);
     assert(SL_RESULT_SUCCESS == result);

     // realize the player
     result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
     assert(SL_RESULT_SUCCESS == result);

     // get the play interface
     result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
     assert(SL_RESULT_SUCCESS == result);

     // get the buffer queue interface
     result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &bqPlayerBufferQueue);
     assert(SL_RESULT_SUCCESS == result);

     // register callback on the buffer queue
     result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, _bqPlayerCallback, NULL);
     assert(SL_RESULT_SUCCESS == result);

     // get the volume interface
     result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
     assert(SL_RESULT_SUCCESS == result);
 }

void audioplayer_init(int32_t samplesPerSec, int32_t framesPerBuffer) {
    debugLog("Device Buffer Size: %d ;  Sample Rate: %d", framesPerBuffer, samplesPerSec);
    global_samplesPerSec = samplesPerSec;
    global_framesPerBuffers = framesPerBuffer;

    _createEngine();
    debugLog("Initialized AudioPlayer");
 }

void audioplayer_startPlayback(void *buffer, size_t bufferSize, int32_t samplesPerSec) {
 // TODO put the init code somewhere else
    // 2 bytes per sample
    //sample_fifo_buffer = calloc(global_bufsize, sizeof(uint16_t));
    //assert(sample_fifo_buffer);
    //audio_utils_fifo_init(&sample_fifo, global_bufsize, 2, sample_fifo_buffer);
    temp_buffer = buffer;
    temp_buffer_size = bufferSize;
    current_temp_buffer_ix = 0;

    if (global_samplesPerSec != samplesPerSec) {
        debugLog("%d != %d", global_samplesPerSec, samplesPerSec);
    }

    _createBufferQueueAudioPlayer(samplesPerSec);

    if (temp_buffer != NULL && bufferSize > 0) {
        // set the player's state to playing
        SLresult result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
        assert(SL_RESULT_SUCCESS == result);
        debugLog("Started playback");

        _bqPlayerCallback(bqPlayerBufferQueue, NULL);
        // WTF!?! this triggers calls to the callback function
        //char empty[1] = {0};
        //(*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, empty, 1);
    } else
        debugLog("WTF!?");
 }

 void audioplayer_stopPlayback() {
    SLresult result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
    assert(SL_RESULT_SUCCESS == result);
    debugLog("Stopped playback");

    //int8_t *buffer = temp_buffer;
    //temp_buffer = NULL;
    //if (buffer != NULL) free(buffer);

    //audio_utils_fifo_deinit(&sample_fifo);
    //free(sample_fifo_buffer);
 }

 // shut down the native audio system
 void audioplayer_cleanup() {
     // destroy buffer queue audio player object, and invalidate all associated interfaces
     if (bqPlayerObject != NULL) {
         (*bqPlayerObject)->Destroy(bqPlayerObject);
         bqPlayerObject = NULL;
         bqPlayerPlay = NULL;
         bqPlayerBufferQueue = NULL;
         //bqPlayerEffectSend = NULL;
         bqPlayerVolume = NULL;
     }

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
 }
