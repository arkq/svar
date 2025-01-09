/*
 * SVAR - writer_sndfile.h
 * SPDX-FileCopyrightText: 2010-2020 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef SVAR_WRITER_SNDFILE_H_
#define SVAR_WRITER_SNDFILE_H_

#include <stdint.h>
#include <sndfile.h>

struct writer_sndfile {
	SNDFILE *sf;
	SF_INFO sfinfo;
};

struct writer_sndfile *writer_sndfile_init(int channels, int sampling, int format);
void writer_sndfile_free(struct writer_sndfile *w);

int writer_sndfile_open(struct writer_sndfile *w, const char *pathname);
void writer_sndfile_close(struct writer_sndfile *w);

ssize_t writer_sndfile_write(struct writer_sndfile *w, int16_t *buffer, size_t frames);

#endif
