#ifndef __UISTATS_H__
#define __UISTATS_H__

#include <string>
#include <android/asset_manager_jni.h>

typedef struct RTPSourceInfo {
    std::string name;
    int jitter;
    int packetLoss;
    int timeOffset;
};

void clearRtpSources();
void addRtpSource(RTPSourceInfo & info);

int getRtpSourceCount();
jstring getRtpSourceName(JNIEnv *env, int index);
int getRtpSourceJitter(JNIEnv *env, int index);
int getRtpSourcePacketLoss(JNIEnv *env, int index);
int getRtpSourceTimeOffset(JNIEnv *env, int index);

#endif // __UISTATS_H__