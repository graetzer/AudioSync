/*
 * audiosync_fifo.h: Implement nonblocking fifo to deal with the data packets.
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

#ifndef AUDIOSYNC_PACKAGE_FIFO_H
#define AUDIOSYNC_PACKAGE_FIFO_H

#define __MAX_UDP_SIZE__ 1472
#define __MIN_UDP_SIZE__ 508
#define MAX_AUDIOSYNC_PACKAGE_SIZE  __MAX_UDP_SIZE__

#ifdef __cplusplus
extern "C" {
#endif

 typedef struct {
     uint16_t seqnum;
     uint32_t timestamp;
     uint16_t pcmDataLength;
     uint8_t pcmData[1];// Actually variable length
 } __attribute__ ((__packed__)) audiosync_package;
 #define AUDIOSYNC_PACKAGE_LENGTH(pkg)  (sizeof(audiosync_package) + pkg->pcmDataLength - 1)

struct audiosync_fifo;
typedef struct audiosync_fifo audiosync_fifo;

// Initialize a FIFO object.
// Input parameters:
//  fifo        Pointer to the FIFO object.
//  frameCount  Max number of significant frames to be stored in the FIFO > 0.
//              If writes and reads always use the same count, and that count is a divisor of
//              frameCount, then the writes and reads will never do a partial transfer.
//  frameSize   Size of each frame in bytes.
void audiosync_fifo_init(audiosync_fifo *fifo, size_t bufferSize);
void audiosync_fifo_uninit(audiosync_fifo *fifo, size_t bufferSize);

#endif
#endif  // !AUDIOSYNC_PACKAGE_FIFO_H