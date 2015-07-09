/*
 * AudioCore.c: Implements the JNi interface and handles
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include "de_rwth_aachen_comsys_audiosync_AudioCore.h"

#include <assert.h>
#include <string.h>
#include <time.h>
#include <jni.h>
#include <pthread.h>
#include <android/log.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "audiosync.h"

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioCore", __VA_ARGS__)
//#define debugLog(...) printf(__VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

#define N_BUFFERS 4
#define MAX_BUFFER_SIZE 4096
int global_bufsize;
int global_sample_rate;
audiosync_context_t audiosync;

int current_temp_buffer_ix = 0;
int16_t temp_buffer[MAX_BUFFER_SIZE * N_BUFFERS];
uint16_t *sample_fifo_buffer=0;
struct audio_utils_fifo* sample_fifo;
static const SLEnvironmentalReverbSettings reverbSettings =
    SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;


/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    initAudio
 * Signature: (II)V
 */
void Java_de_rwth_1aachen_comsys_audiosync_AudioCore_initAudio (JNIEnv *env, jobject thiz, jint sample_rate, jint buf_size) {
    global_sample_rate = sample_rate;
    global_bufsize = buf_size;
}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startPlayback
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startStreaming(JNIEnv *env, jobject thiz, jstring jPath) {

}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    stopPlayback
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_stopPlayback (JNIEnv *env, jobject thiz) {

}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startListening
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startListening(JNIEnv *env, jobject thiz, jstring jHost) {
    const char *host = (*env)->GetStringUTFChars(env, jHost, 0);
    audiosync_addClient(&audiosync, host);
    (*env)->ReleaseStringUTFChars(env, jHost, host);
 }

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
 static SLEffectSendItf bqPlayerEffectSend;
 static SLVolumeItf bqPlayerVolume;

 // create the engine and output mix objects
 void createEngine(JNIEnv* env, jclass clazz) {
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
 void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
     assert(bq == bqPlayerBufferQueue);
     assert(NULL == context);
     // for streaming playback, replace this test by logic to find and fill the next buffer
     /*if (--nextCount > 0 && NULL != nextBuffer && 0 != nextSize) {
         SLresult result;
         // enqueue another buffer
         result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, nextBuffer, nextSize);
         // the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
         // which for this code example would indicate a programming error
         assert(SL_RESULT_SUCCESS == result);
     }*/

    int16_t *buf_ptr = temp_buffer + global_bufsize * current_temp_buffer_ix;

    ssize_t frameCount = audio_utils_fifo_read(sample_fifo, buf_ptr, global_bufsize);
    if (frameCount > 0) {
    SLresult result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buf_ptr, frameCount * 2);
        assert(SL_RESULT_SUCCESS == result);
        current_temp_buffer_ix = (current_temp_buffer_ix + 1) % N_BUFFERS;
    }
 }

 // create buffer queue audio player
 void createBufferQueueAudioPlayer() {
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
     const SLInterfaceID ids[3] = {SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND,
             /*SL_IID_MUTESOLO,*/ SL_IID_VOLUME};
     const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,
             /*SL_BOOLEAN_TRUE,*/ SL_BOOLEAN_TRUE};
     result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk,
             3, ids, req);
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
     result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, NULL);
     assert(SL_RESULT_SUCCESS == result);

     // get the effect send interface
     result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_EFFECTSEND,
             &bqPlayerEffectSend);
     assert(SL_RESULT_SUCCESS == result);

     // get the volume interface
     result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
     assert(SL_RESULT_SUCCESS == result);
 }

 void startPlayback() {
 // TODO put the init code somewehere ese
    // 2 bytes per sample
    sample_fifo_buffer = calloc(global_bufsize, sizeof(uint16_t));
    assert(sample_fifo_buffer);
    audio_utils_fifo_init(&sample_fifo, global_bufsize, 2, sample_fifo_buffer);

    // set the player's state to playing
    SLresult result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
    assert(SL_RESULT_SUCCESS == result);
 }

 void stopPlayback() {
    SLresult result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
    assert(SL_RESULT_SUCCESS == result);

    audio_utils_fifo_deinit(&sample_fifo);
    free(sample_fifo_buffer);
 }

 // shut down the native audio system
 void shutdownCleanup() {
     // destroy buffer queue audio player object, and invalidate all associated interfaces
     if (bqPlayerObject != NULL) {
         (*bqPlayerObject)->Destroy(bqPlayerObject);
         bqPlayerObject = NULL;
         bqPlayerPlay = NULL;
         bqPlayerBufferQueue = NULL;
         bqPlayerEffectSend = NULL;
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

#ifdef __cplusplus
}
#endif
#endif
