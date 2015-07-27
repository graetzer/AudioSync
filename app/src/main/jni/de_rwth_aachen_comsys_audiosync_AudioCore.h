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
 * Method:    startStreaming
 * Signature: (ILandroid/content/res/AssetManager;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_startStreaming
  (JNIEnv *, jobject, jint, jobject, jstring);

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
 * Method:    getRtpSourceCount
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getRtpSourceCount
  (JNIEnv *, jobject);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    getRtpSourceName
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getRtpSourceName
  (JNIEnv *, jobject, jint);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    getRtpSourceJitter
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getRtpSourceJitter
  (JNIEnv *, jobject, jint);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    getRtpSourcePacketsLost
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getRtpSourcePacketsLost
  (JNIEnv *, jobject, jint);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    getRtpSourceTimeOffset
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getRtpSourceTimeOffset
  (JNIEnv *, jobject, jint);

/*
 * Class:     de_rwth_aachen_comsys_audiosync_AudioCore
 * Method:    getRtpSourceSender
 * Signature: (I)Z
 */
JNIEXPORT jboolean JNICALL Java_de_rwth_1aachen_comsys_audiosync_AudioCore_getRtpSourceSender
  (JNIEnv *, jobject, jint);

#ifdef __cplusplus
}
#endif
#endif
