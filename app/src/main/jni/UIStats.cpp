#include "UIStats.h"

#include <pthread.h>

#include <vector>
#include <string>

std::vector<RTPSourceInfo> rtpSources;

void clearRtpSources() {
    rtpSources.clear();
}

void addRtpSource(RTPSourceInfo & info) {
    rtpSources.push_back(info);
}

int getRtpSourceCount() {
    return rtpSources.size();
}

jstring getRtpSourceName(JNIEnv *env, int index) {
    if(index < 0 || index >= rtpSources.size())
        return 0;
    return env->NewStringUTF(rtpSources[index].name.c_str());
}

int getRtpSourceJitter(JNIEnv *env, int index) {
    if(index < 0 || index >= rtpSources.size())
        return 0;

    return rtpSources[index].jitter;
}

int getRtpSourcePacketLoss(JNIEnv *env, int index)
 {
    if(index < 0 || index >= rtpSources.size())
        return 0;

    return rtpSources[index].packetLoss;
}

int getRtpSourceTimeOffset(JNIEnv *env, int index)
{
    if(index < 0 || index >= rtpSources.size())
        return 0;

    return rtpSources[index].timeOffset;
}