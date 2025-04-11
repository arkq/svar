/*
 * SVAR - writer.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_WRITER_H_
#define SVAR_WRITER_H_

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum writer_format {
	WRITER_FORMAT_RAW,
#if ENABLE_SNDFILE
	WRITER_FORMAT_WAV,
#endif
#if ENABLE_MP3LAME
	WRITER_FORMAT_MP3,
#endif
#if ENABLE_VORBIS
	WRITER_FORMAT_OGG,
#endif
};

struct writer {
	enum writer_format format;
	bool opened;
	/* implementation specific functions */
	int (*open)(struct writer * w, const char * pathname);
	ssize_t (*write)(struct writer * w, int16_t * buffer, size_t frames);
	void (*close)(struct writer * w);
	void (*free)(struct writer * w);
	/* implementation specific data */
	void * w;
};

struct writer * writer_raw_new(unsigned int channels);
const char * writer_format_to_string(enum writer_format format);

#endif
