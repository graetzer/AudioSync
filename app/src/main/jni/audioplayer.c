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

 #include "audioplayer.h"



// engine interfaces
 static SLObjectItf engineObject = NULL;
 static SLEngineItf engineEngine;

 // output mix interfaces
 static SLObjectItf outputMixObject = NULL;
 static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;

 // buffer queue player interfaces
 static SLObjectItf bqPlayerObject = NULL;
 static SLPlayItf bqPlayerPlay;
 static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
 //static SLEffectSendItf bqPlayerEffectSend;
 static SLVolumeItf bqPlayerVolume;

 // Parameters for playback
 static int global_bufsize;
 static size_t global_sample_rate;

 /*

 #define N_BUFFERS 4
 #define MAX_BUFFER_SIZE 4096
  int16_t temp_buffer[MAX_BUFFER_SIZE * N_BUFFERS];
  uint16_t *sample_fifo_buffer=0;*/

// Current data to play
 static size_t current_temp_buffer_ix = 0
 ;
 static void * temp_buffer;
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

     // realize the output mix
     result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
     assert(SL_RESULT_SUCCESS == result);

     // get the environmental reverb interface
     // this could fail if the environmental reverb effect is not available,
     // either because the feature is not present, excessive CPU load, or
     // the required MODIFY_AUDIO_SETTINGS permission was not requested and granted
     result = (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB,
             &outputMixEnvironmentalReverb);
     if (SL_RESULT_SUCCESS == result) {
         result = (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
                 outputMixEnvironmentalReverb, &reverbSettings);
         (void)result;
     }
     // ignore unsuccessful result codes for environmental reverb, as it is optional for this example
 }

 // this callback handler is called every time a buffer finishes playing
static void _bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
     assert(bq == bqPlayerBufferQueue);
     assert(NULL == context);

     // Assuming PCM 16
     int16_t *buf_ptr = temp_buffer + global_bufsize * current_temp_buffer_ix;
     if (temp_buffer_size < global_bufsize * current_temp_buffer_ix) {
        SLresult result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buf_ptr, global_bufsize);
        assert(SL_RESULT_SUCCESS == result);
        current_temp_buffer_ix++;
     } else {
        // Technically wrong
        audioplayer_stopPlayback();
     }

    /*int16_t *buf_ptr = temp_buffer + global_bufsize * current_temp_buffer_ix;

    ssize_t frameCount = audio_utils_fifo_read(sample_fifo, buf_ptr, global_bufsize);
    if (frameCount > 0) {
    SLresult result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buf_ptr, frameCount * 2);
        assert(SL_RESULT_SUCCESS == result);
        current_temp_buffer_ix = (current_temp_buffer_ix + 1) % N_BUFFERS;
    }*/
 }

 // create buffer queue audio player
 static void _createBufferQueueAudioPlayer(int sample_rate) {
     SLresult result;

     // configure audio source
     SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
     SLDataFormat_PCM format_pcm = {
         	.formatType = SL_DATAFORMAT_PCM,
         	.numChannels = 1,
         	.samplesPerSec = global_sample_rate * 1000,// Milli Hz
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
     const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, /*SL_IID_EFFECTSEND, SL_IID_MUTESOLO,*/ SL_IID_VOLUME};
     const SLboolean req[2] = {SL_BOOLEAN_TRUE, /*SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,*/ SL_BOOLEAN_TRUE};
     result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk, 2, ids, req);
     assert(SL_RESULT_SUCCESS == result);

     // realize the player
     result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
     assert(SL_RESULT_SUCCESS == result);

     // get the play interface
     result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
     assert(SL_RESULT_SUCCESS == result);

     // get the buffer queue interface
     result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE, &bqPlayerBufferQueue);
     assert(SL_RESULT_SUCCESS == result);

     // register callback on the buffer queue
     result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue,_ bqPlayerCallback, NULL);
     assert(SL_RESULT_SUCCESS == result);

     // get the effect send interface
     //result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_EFFECTSEND,
     //        &bqPlayerEffectSend);
     //assert(SL_RESULT_SUCCESS == result);

     // get the volume interface
     result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
     assert(SL_RESULT_SUCCESS == result);
 }

 static void _cleanupEngine() {

 }

void audioplayer_init(int sample_rate, size_t buf_size) {
    global_sample_rate = sample_rate;
    global_bufsize = buf_size;
    _createEngine();
    _createBufferQueueAudioPlayer();
 }

void audioplayer_startPlayback(const void *buffer, const size_t bufferSize) {
 // TODO put the init code somewhere else
    // 2 bytes per sample
    //sample_fifo_buffer = calloc(global_bufsize, sizeof(uint16_t));
    //assert(sample_fifo_buffer);
    //audio_utils_fifo_init(&sample_fifo, global_bufsize, 2, sample_fifo_buffer);

    // set the player's state to playing
    SLresult result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
    assert(SL_RESULT_SUCCESS == result);
 }

 void audioplayer_stopPlayback() {
    SLresult result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
    assert(SL_RESULT_SUCCESS == result);

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
         outputMixEnvironmentalReverb = NULL;
     }

     // destroy engine object, and invalidate all associated interfaces
     if (engineObject != NULL) {
         (*engineObject)->Destroy(engineObject);
         engineObject = NULL;
         engineEngine = NULL;
     }
 }
