/*
 * decoder.c: Decode an audio file
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <android/log.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#define debugLog(...) __android_log_print(ANDROID_LOG_DEBUG, "Decoder", __VA_ARGS__)

void* decodeMusicfile(char *filepath) {

}