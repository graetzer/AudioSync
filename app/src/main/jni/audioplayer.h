/*
 * audioplayer.h: Encapsulate the OpenSL interface into something simpler to use
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

// Initialize the OpenSL audio interface
void audioplayer_init(int sample_rate, size_t buf_size);
void audioplayer_startPlayback(const void *buffer, const size_t bufferSize);
void audioplayer_stopPlayback();
void audioplayer_cleanup();