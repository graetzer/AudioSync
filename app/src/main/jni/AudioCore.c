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

#include <jni.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioCore", __VA_ARGS__)
//#define debugLog(...) printf(__VA_ARGS__)

#include "audioplayer.h"
#include "audiostream.h"
#include "decoder.h"

#ifdef RTP_SUPPORT_THREAD
void thread_exit_handler(int sig) {
    debugLog("this signal is %d \n", sig);
    pthread_exit(0);
}
#endif

audiosync_context audioCtx;

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    initAudio
 * Signature: (II)V
 */
void Java_de_rwth_1aachen_comsys_audiosync_AudioCore_initAudio (JNIEnv *env, jobject thiz, jint samplesPerSec, jint framesPerBuffer) {
    audioplayer_init(samplesPerSec, framesPerBuffer);

#ifdef RTP_SUPPORT_THREAD
    // Workaround to kill threads since pthread_cancel is not supported
    // See jthread.cpp
    struct sigaction actions;
    memset(&actions, 0, sizeof(actions));
    sigemptyset(&actions.sa_mask);
    actions.sa_flags = 0;
    actions.sa_handler = thread_exit_handler;
    sigaction(SIGUSR1, &actions, NULL);
#endif
}

void Java_de_rwth_1aachen_comsys_audiosync_AudioCore_deinitAudio (JNIEnv *env, jobject thiz) {
    audioplayer_cleanup();
}

//static const char android[] =
//#include "android_clip.h"
//;



/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startPlayback
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startStreaming(JNIEnv *env, jobject thiz, jobject assetManager, jstring jPath) {
    //void *buffer = memcpy(malloc(sizeof(android)), android, sizeof(android));
    //audioplayer_startPlayback(buffer, sizeof(android), 8000, 1);/*

    // Get the asset manager
    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    // Open the asset from the assets/ folder
    const char *path = (*env)->GetStringUTFChars(env, jPath, 0);
    AAsset *asset = AAssetManager_open(mgr, path, AASSET_MODE_UNKNOWN);
    if (NULL == asset) {
        debugLog("_ASSET_NOT_FOUND_");
        return;
    }

    off_t outStart, fileSize;
    int fd = AAsset_openFileDescriptor(asset, &outStart, &fileSize);
    assert(0 <= fd);
    AAsset_close(asset);

    AMediaExtractor *extr = decoder_createExtractor(fd, outStart, fileSize);
    audiosync_startSending(&audioCtx, extr);

    /*debugLog("%s size: %ld %ld", path, (long)fileSize, (long)outStart);
    struct decoder_audio audio = decoder_decodeFile(fd, outStart, fileSize);
    if (audio.pcmLength > 0)
        audioplayer_startPlayback(audio.pcm, (size_t)audio.pcmLength, audio.sampleRate, audio.numChannels);
    else
        debugLog("Decoding seems to have failed");
    (*env)->ReleaseStringUTFChars(env, jPath, path);
    //*/
}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    stopPlayback
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_stopPlayback (JNIEnv *env, jobject thiz) {
    audioplayer_stopPlayback();
    audiosync_stop(&audioCtx);
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
