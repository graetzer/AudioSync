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
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <signal.h>
#include <assert.h>
#include "audioplayer.h"
#include "AudioStreamSession.h"
#include "SenderSession.h"
#include "ReceiverSession.h"
#include "decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "AudioCore", __VA_ARGS__)
//#define debugLog(...) printf(__VA_ARGS__)

#ifdef RTP_SUPPORT_THREAD
void thread_exit_handler(int sig) {
    debugLog("this signal is %d \n", sig);
    pthread_exit(0);
}
#endif

// Global audiostream manager
AudioStreamSession *audioSession;

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    initAudio
 * Signature: (II)V
 */
void Java_de_rwth_1aachen_comsys_audiosync_AudioCore_initAudio (JNIEnv *env, jobject thiz, jint samplesPerSec, jint framesPerBuffer) {
    audioplayer_initGlobal((uint32_t)samplesPerSec, (uint32_t)framesPerBuffer);

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
    if (audioSession) audioSession->Stop();
    if (audioSession) delete audioSession;
    audioplayer_cleanup();// Player stops automatically
}

//static const char android[] =
//#include "android_clip.h"
//;

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startPlayback
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startStreaming(JNIEnv *env, jobject thiz, jint portbase, jobject assetManager, jstring jPath) {
    //void *buffer = memcpy(malloc(sizeof(android)), android, sizeof(android));
    //audioplayer_startPlayback(buffer, sizeof(android), 8000, 1);/*

    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    const char *path = env->GetStringUTFChars(jPath, 0);
    // Open the asset from the assets/ folder
    AAsset *asset = AAssetManager_open(mgr, path, AASSET_MODE_UNKNOWN);
    env->ReleaseStringUTFChars(jPath, path);
    if (NULL == asset) {
        debugLog("_ASSET_NOT_FOUND_");
        return;
    }

    off_t outStart, fileSize;
    int fd = AAsset_openFileDescriptor(asset, &outStart, &fileSize);
    assert(0 <= fd);

    debugLog("Audio file offset: %ld, size: %ld", outStart, fileSize);
    AMediaExtractor *extr = decoder_createExtractor(fd, outStart, fileSize);
    audioSession = SenderSession::StartStreaming((uint16_t)portbase, extr);//*/

    AAsset_close(asset);

    /*struct decoder_audio audio = decoder_decodeFile(fd, outStart, fileSize);
    debugLog("Decoded size %ld", (long) audio.pcmLength);
    if (audio.pcmLength > 0)
        audioplayer_startPlayback(audio.pcm, (size_t)audio.pcmLength, audio.sampleRate, audio.numChannels);
    else
        debugLog("Decoding seems to have failed");

    //*/
}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startReceiving
 * Signature: (Ljava/lang/String;I)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startReceiving (JNIEnv *env, jobject thiz, jstring jHost, jint portbase) {
    const char *host = env->GetStringUTFChars(jHost, 0);
    audioSession = ReceiverSession::StartReceiving(host, (uint16_t)portbase);
    env->ReleaseStringUTFChars(jHost, host);
}

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    stop
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_stopServices(JNIEnv *env, jobject thiz) {
    audioplayer_stopPlayback();
    if (audioSession) audioSession->Stop();
}

#ifdef __cplusplus
}
#endif
