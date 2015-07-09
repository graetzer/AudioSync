/*
 * decoder.h: Decode an audio file, put the PCM data into a buffer
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef AUDIOSYNC_DECODE_H
#define AUDIOSYNC_DECODE_H


ssize_t decode_audiofile(int fd, off_t fileSize, uint8_t **pcmOut, int32_t *bitRate, int32_t *sampleRate);

#endif