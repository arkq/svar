/*
 * SVAR - writer_mp3lame.h
 * SPDX-FileCopyrightText: 2010-2020 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef SVAR_WRITER_MP3LAME_H_
#define SVAR_WRITER_MP3LAME_H_

#include <stdint.h>
#include <stdio.h>
#include <lame/lame.h>

struct writer_mp3lame {
	lame_global_flags *gfp;
	unsigned char mp3buf[1024 * 64];
	FILE *fp;
};

struct writer_mp3lame *writer_mp3lame_init(int channels, int sampling,
		int bitrate_min, int bitrate_max, const char *comment);
void writer_mp3lame_free(struct writer_mp3lame *w);

int writer_mp3lame_open(struct writer_mp3lame *w, const char *pathname);
void writer_mp3lame_close(struct writer_mp3lame *w);

ssize_t writer_mp3lame_write(struct writer_mp3lame *w, int16_t *buffer, size_t frames);

#endif
