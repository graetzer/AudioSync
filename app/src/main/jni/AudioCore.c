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

//#include "audiosync.h"
#include "audioplayer.h"

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



#ifdef __cplusplus
}
#endif
