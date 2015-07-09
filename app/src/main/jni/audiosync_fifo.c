/*
 * audiosync_fifo.c: Implement nonblocking fifo to deal with the data packets.
 * Will only work with one reader and one writer
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <stdlib.h>
#include <string.h>

#include "audiosync_fifo.h"

struct audiosync_fifo {
    size_t mBufferSize;
    size_t *mBufferOffsets// Offset into the buffer for each package
    audiosync_package *mBuffer; // pointer to buffer of size mFrameCount frames
    volatile int32_t mFront; // frame index of first package slot available to read, or read index
    volatile int32_t mRear;  // frame index of next package slot available to write, or write index
};

typedef struct audiosync_fifo audiosync_fifo;

void audiosync_fifo_init(audiosync_fifo *fifo, size_t bufferSize) {
    assert(__MAX_UDP_SIZE__ >= sizeof(audiosync_package));
    fifo->mBufferSize = bufferSize;
    fifo->mBufferOffsets = calloc(bufferSize, sizeof(size_t));
    fifo->mBuffer = calloc(bufferSize, __MAX_UDP_SIZE__);
}

void audiosync_fifo_realloc(audiosync_fifo *fifo, size_t bufferSize) {
    fifo->mBufferSize = bufferSize;
    fifo->mBufferOffsets = realloc(fifo->mBufferOffsets, bufferSize * sizeof(size_t));
    fifo->mBuffer = realloc(fifo->mBuffer, bufferSize * __MAX_UDP_SIZE__);
}

void audiosync_fifo_uninit(audiosync_fifo *fifo, size_t bufferSize) {
    free(fifo->mBuffer);
    free(fifo->mBufferOffsets);

    android_atomic_release_store(audio_utils_fifo_sum(fifo, fifo->mRear, availToWrite),
                    &fifo->mRear);
}

ssize_t audiosync_fifo_write(struct audio_utils_fifo *fifo, const audiosync_package *package) {
    int32_t front = android_atomic_acquire_load(&fifo->mFront);
    int32_t rear = fifo->mRear;
    if ((mRear++) % fifo->mBufferSize + 1 == front) return -1;

    size_t length = AUDIOSYNC_PACKAGE_LENGTH(package);
    memcpy(&)

    android_atomic_release_store((mRear++) % fifo->mBufferSize, &fifo->mRear);
    return 0;
}

ssize_t audio_utils_fifo_read(struct audio_utils_fifo *fifo, audiosync_package *buffer) {
    int32_t rear = android_atomic_acquire_load(&fifo->mRear);
    int32_t front = fifo->mFront;
    if (front+1 >= rear) return -1;

    audiosync_package *package = fifo->mBuffer + fifo->mBufferOffsets[front];
    size_t length = AUDIOSYNC_PACKAGE_LENGTH(package);
    memcpy(buffer, package, length);

    android_atomic_release_store((mFront++) % fifo->mBufferSize, &fifo->mFront);
    return length;
}