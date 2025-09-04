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
#include <sys/types.h>

#include "pcm.h"

enum writer_type {
	WRITER_TYPE_RAW,
#if ENABLE_SNDFILE
	WRITER_TYPE_WAV,
	WRITER_TYPE_RF64,
#endif
#if ENABLE_MP3LAME
	WRITER_TYPE_MP3,
#endif
#if ENABLE_VORBIS
	WRITER_TYPE_OGG,
#endif
};

struct writer {
	enum writer_type type;
	bool opened;
	/* implementation specific functions */
	int (*open)(struct writer * w, const char * pathname);
	ssize_t (*write)(struct writer * w, const void * buffer, size_t frames);
	void (*close)(struct writer * w);
	void (*free)(struct writer * w);
	/* implementation specific data */
	void * w;
};

struct writer * writer_raw_new(enum pcm_format format, unsigned int channels);
const char * writer_type_to_string(enum writer_type type);

#endif
