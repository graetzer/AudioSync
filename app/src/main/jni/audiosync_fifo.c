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
    volatile int32_t mFront; // frame index of first frame slot available to read, or read index
    volatile int32_t mRear;  // frame index of next frame slot available to write, or write index
};

typedef struct audiosync_fifo audiosync_fifo;

void audiosync_fifo_init(audiosync_fifo *fifo, size_t bufferSize) {
    assert(__MAX_UDP_SIZE__ >= sizeof(audiosync_package));
    fifo->mBufferSize = bufferSize;
    fifo->mBufferOffsets = calloc(bufferSize, sizeof(size_t));
    fifo->mBuffer = calloc(bufferSize, __MAX_UDP_SIZE__);
}

void audiosync_fifo_uninit(audiosync_fifo *fifo, size_t bufferSize) {
    free(fifo->mBuffer);
    free(fifo->mBufferOffsets);
}

ssize_t audiosync_fifo_write(struct audio_utils_fifo *fifo, const audiosync_package *package) {

}

ssize_t audio_utils_fifo_read(struct audio_utils_fifo *fifo, void *buffer, size_t count) {
    int32_t rear = android_atomic_acquire_load(&fifo->mRear);
    int32_t front = fifo->mFront;
    size_t availToRead = audio_utils_fifo_diff(fifo, rear, front);
    if (availToRead > count) {
        availToRead = count;
    }
}