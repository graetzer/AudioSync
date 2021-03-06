/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class de_rwth_aachen_comsys_audiosync_AudioCore */

#ifndef _Included_de_rwth_aachen_comsys_audiosync_AudioCore
#define _Included_de_rwth_aachen_comsys_audiosync_AudioCore
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    initAudio
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_initAudio
  (JNIEnv *, jobject, jint, jint);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    deinitAudio
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_deinitAudio
  (JNIEnv *, jobject);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startStreamingAsset
 * Signature: (ILandroid/content/res/AssetManager;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startStreamingAsset
  (JNIEnv *, jobject, jint, jobject, jstring);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startStreamingUri
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startStreamingUri
        (JNIEnv *, jobject, jint, jstring);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    startReceiving
 * Signature: (Ljava/lang/String;I)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startReceiving
  (JNIEnv *, jobject, jstring, jint);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    stopServices
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_stopServices
  (JNIEnv *, jobject);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    setDeviceLatency
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_setDeviceLatency
        (JNIEnv *, jobject,  jlong);

/*
 * Class:     Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getAudioDestinations
 * Method:    getRtpSourceCount
 * Signature: ()[Lde/rwth_aachen/comsys/audiosync/AudioDestination;
 */
JNIEXPORT jobjectArray JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getAudioDestinations
  (JNIEnv *, jobject);

JNIEXPORT jlong JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getCurrentPresentationTime
        (JNIEnv *, jobject);

JNIEXPORT jboolean JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_isRunning
        (JNIEnv *, jobject);

JNIEXPORT jboolean JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_isSending
        (JNIEnv *, jobject);

JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_pauseSending
        (JNIEnv *, jobject);

#ifdef __cplusplus
}
#endif
#endif
