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
#include <android/log.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <android/asset_manager.h>

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioCore", __VA_ARGS__)
//#define debugLog(...) printf(__VA_ARGS__)

//#include "audiosync.h"
#include "audioplayer.h"
#include "decoder.c"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    initAudio
 * Signature: (II)V
 */
void Java_de_rwth_1aachen_comsys_audiosync_AudioCore_initAudio (JNIEnv *env, jobject thiz, jint sample_rate, jint buf_size) {
    global_sample_rate = sample_rate;
    global_bufsize = buf_size;
    audioplayer_init(sample_rate, (size_t)buf_size);
}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startPlayback
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startStreaming(JNIEnv *env, jobject thiz, jobject assetManager, jstring jPath) {
    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    const char *path = (*env)->GetStringUTFChars(env, jPath, 0);
    //audiosync_addClient(&audiosync, host);
    (*env)->ReleaseStringUTFChars(env, jPath, path);

    AAsset* asset = AAssetManager_open(mgr, path, AASSET_MODE_UNKNOWN);
    if (NULL == asset) {
        debugLog("_ASSET_NOT_FOUND_");
        return;
    }

    off_t fileSize = AAsset_getLength(asset);
    off_t outStart = 0;
    int fd = AAsset_openFileDescriptor(asset, &outStart, &fileSize);

    uint8_t *pcmOut, uint32_t bitRate, uint32_t sampleRate;
    ssize_t bufferSize = decodeAudiofile(fd, fileSize, &pcmOut, &bitRate, &sample_rate);
    if (bufferSize > 0)
        audioplayer_startPlayback(pcmOut, (size_t)bufferSize);
}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    stopPlayback
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_stopPlayback (JNIEnv *env, jobject thiz) {
    audioplayer_stopPlayback();
}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startListening
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startListening(JNIEnv *env, jobject thiz, jstring jHost) {
    //const char *host = (*env)->GetStringUTFChars(env, jHost, 0);
    //audiosync_addClient(&audiosync, host);
    //(*env)->ReleaseStringUTFChars(env, jHost, host);
 }



#ifdef __cplusplus
}
#endif
