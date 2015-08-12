/*
 * apppacket.c: helper function to create and configure a AMediaFormat intance
 *
 * (C) Copyright 2015 Simon Grätzer
 * Email: simon@graetzer.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include "apppacket.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

const uint8_t *audiostream_app_name = (const uint8_t*) "ADST";

AMediaFormat *audiostream_createFormat(char *formatString) {
    // An example should look like this
    // mime: string(audio/mpeg), durationUs: int64(56737959), bit-rate: int32(128000), channel-count: int32(2),
    // sample-rate: int32(44100), encoder-delay: int32(576), encoder-padding: int32(1579)}


    AMediaFormat *format = AMediaFormat_new();
    char *attr = strtok(formatString, ", ");
    do {
        char *type = strchr(attr, ':');
        if (type == NULL) continue;
        char *valBegin = strchr(attr, '(');
        if (valBegin == NULL) continue;
        char *end = strchr(valBegin, ')');
        if (end == NULL) continue;

        type = '\0';
        valBegin++;
        type++;
        char *endptr;
        if (strncmp(type, "int32", 5) == 0) {
            long val = strtol(valBegin, &endptr, 10);
            if (endptr == end) {
                AMediaFormat_setInt32(format, attr, (int32_t)val);
            }
        } else if (strncmp(type, "int64", 5) == 0) {
            long val = strtol(valBegin, &endptr, 10);
            if (endptr == end) {
                AMediaFormat_setInt64(format, attr, (int64_t)val);
            }
        } else if (strncmp(type, "size_t", 6) == 0) {
            // No set function
        } else if (strncmp(attr, "float", 5) == 0 || strncmp(type, "double", 6) == 0) {
            double val = strtod(valBegin, &endptr);
            if (endptr == end) {
                AMediaFormat_setFloat(format, attr, (float)val);
            }
        } else if (strncmp(type, "string", 6) == 0) {
            *end = '\0';
            AMediaFormat_setString(format, attr, valBegin);
        } else {
            // probably a ByteBuffer, there is not value given
        }

    } while((attr = strtok(NULL, ", ")) != NULL);
    return format;

    /* TODO for certain codecs we might have to set some  buffer values
     MediaFormat mf = MediaFormat.createAudioFormat ("audio/mp4a-latm", getSampleRate(), getChannelCount());
     mediaFormat.setInteger(MediaFormat.KEY_IS_ADTS, 1);
     byte[] bytes = new byte[]{(byte) 0x11, (byte)0x90};
ByteBuffer bb = ByteBuffer.wrap(bytes);
mf.setByteBuffer("csd-0", bb);
     * */
}

int64_t audiosync_systemTimeUs() {
    struct timespec ts;
    int err = clock_gettime(CLOCK_REALTIME, &ts);
    if (err) return 0;
    int64_t now = ts.tv_sec*100000 + ts.tv_nsec/1000;
    return now;
}

