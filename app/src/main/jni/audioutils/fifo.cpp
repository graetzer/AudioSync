/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fifo.h"
#include <android/log.h>

#define LOG_TAG "audio_utils_fifo"
#ifndef ALOG_ASSERT
#define ALOG_ASSERT(cond, ...) \
    ( (!(cond)) \
    ? ((void)__android_log_assert(#cond, LOG_TAG, #__VA_ARGS__)) \
    : (void)0 )
#endif

using namespace std;

unsigned roundup(unsigned v) {
    // __builtin_clz is undefined for zero input
    if (v == 0) {
        v = 1;
    }
    int lz = __builtin_clz((int) v);
    unsigned rounded = ((unsigned) 0x80000000) >> lz;
    // 0x800000001 and higher are actually rounded _down_ to prevent overflow
    if (v > rounded && lz > 0) {
        rounded <<= 1;
    }
    return rounded;
}


void audio_utils_fifo_init(struct audio_utils_fifo *fifo, size_t frameCount, size_t frameSize,
                           void *buffer)
{
    // We would need a 64-bit roundup to support larger frameCount.
    ALOG_ASSERT(fifo != NULL && frameCount > 0 && frameSize > 0 && buffer != NULL, "Fifo init");
    fifo->mFrameCount = frameCount;
    fifo->mFrameCountP2 = roundup((unsigned)frameCount);
    fifo->mFudgeFactor = fifo->mFrameCountP2 - fifo->mFrameCount;
    fifo->mFrameSize = frameSize;
    fifo->mBuffer = buffer;
    fifo->mFront = 0;
    fifo->mRear = 0;
}
void audio_utils_fifo_deinit(struct audio_utils_fifo *fifo __unused)
{
}
// Return a new index as the sum of an old index (either mFront or mRear) and a specified increment.
static inline int32_t audio_utils_fifo_sum(struct audio_utils_fifo *fifo, int32_t index,
                                           uint32_t increment)
{
    if (fifo->mFudgeFactor) {
        uint32_t mask = fifo->mFrameCountP2 - 1;
        ALOG_ASSERT((index & mask) < fifo->mFrameCount);
        ALOG_ASSERT(/*0 <= increment &&*/ increment <= fifo->mFrameCountP2);
        if ((index & mask) + increment >= fifo->mFrameCount) {
            increment += fifo->mFudgeFactor;
        }
        index += increment;
        ALOG_ASSERT((index & mask) < fifo->mFrameCount);
        return index;
    } else {
        return index + increment;
    }
}
// Return the difference between two indices: rear - front, where 0 <= difference <= mFrameCount.
static inline size_t audio_utils_fifo_diff(struct audio_utils_fifo *fifo, int32_t rear,
                                           int32_t front)
{
    int32_t diff = rear - front;
    if (fifo->mFudgeFactor) {
        uint32_t mask = ~(fifo->mFrameCountP2 - 1);
        int32_t genDiff = (rear & mask) - (front & mask);
        if (genDiff != 0) {
            ALOG_ASSERT(genDiff == (int32_t) fifo->mFrameCountP2);
            diff -= fifo->mFudgeFactor;
        }
    }
    // FIFO should not be overfull
    ALOG_ASSERT(0 <= diff && diff <= (int32_t) fifo->mFrameCount);
    return (size_t) diff;
}
ssize_t audio_utils_fifo_write(struct audio_utils_fifo *fifo, const void *buffer, size_t count)
{
    int32_t front = atomic_load_explicit(&fifo->mFront, memory_order_acquire);
    int32_t rear = fifo->mRear;
    size_t availToWrite = fifo->mFrameCount - audio_utils_fifo_diff(fifo, rear, front);
    if (availToWrite > count) {
        availToWrite = count;
    }
    rear &= fifo->mFrameCountP2 - 1;
    size_t part1 = fifo->mFrameCount - rear;
    if (part1 > availToWrite) {
        part1 = availToWrite;
    }
    if (part1 > 0) {
        memcpy((char *) fifo->mBuffer + (rear * fifo->mFrameSize), buffer,
               part1 * fifo->mFrameSize);
        size_t part2 = availToWrite - part1;
        if (part2 > 0) {
            memcpy(fifo->mBuffer, (char *) buffer + (part1 * fifo->mFrameSize),
                   part2 * fifo->mFrameSize);
        }
        atomic_store_explicit(&fifo->mRear,
                              audio_utils_fifo_sum(fifo, fifo->mRear, availToWrite),
                              memory_order_release);
    }
    return availToWrite;
}
ssize_t audio_utils_fifo_read(struct audio_utils_fifo *fifo, void *buffer, size_t count)
{
    int32_t rear = atomic_load_explicit(&fifo->mRear, memory_order_acquire);
    int32_t front = fifo->mFront;
    size_t availToRead = audio_utils_fifo_diff(fifo, rear, front);
    if (availToRead > count) {
        availToRead = count;
    }
    front &= fifo->mFrameCountP2 - 1;
    size_t part1 = fifo->mFrameCount - front;
    if (part1 > availToRead) {
        part1 = availToRead;
    }
    if (part1 > 0) {
        memcpy(buffer, (char *) fifo->mBuffer + (front * fifo->mFrameSize),
               part1 * fifo->mFrameSize);
        size_t part2 = availToRead - part1;
        if (part2 > 0) {
            memcpy((char *) buffer + (part1 * fifo->mFrameSize), fifo->mBuffer,
                   part2 * fifo->mFrameSize);
        }
        atomic_store_explicit(&fifo->mFront,
                              audio_utils_fifo_sum(fifo, fifo->mFront, availToRead),
                              memory_order_release);
    }
    return availToRead;
}
size_t audio_utils_fifo_available(struct audio_utils_fifo *fifo) {
    // This will be called by the writer thread, hope this works out
    // doesn't really matter if this is like 100% correct
    int32_t front = fifo->mFront;
    int32_t rear = (int32_t) fifo->mRear;
    size_t availToRead = audio_utils_fifo_diff(fifo, rear, front);
    return availToRead;
}