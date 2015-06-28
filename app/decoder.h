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
// As a side note, we need to figure out what to do if the bitrate isn't 16 bit per sample
// Also how do we do on the fly resampling when we inevitably have an device with 44.1 kHz
// and an audio file with 48kHz ?
ssize_t decodeAudiofile(int fd, uint8_t **pcmOut, uint32_t *bitRate, uint32_t *sampleRate);