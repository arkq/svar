/*
 * SVAR - writer.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct writer_raw {
	unsigned int channels;
	FILE * f;
};

static int writer_raw_open(struct writer * writer, const char * pathname) {
	struct writer_raw * w = writer->w;

	writer->close(writer);
	if ((w->f = fopen(pathname, "w")) != NULL)
		return -1;

	writer->opened = true;
	return 0;
}

static ssize_t writer_raw_write(struct writer * writer, int16_t * buffer, size_t frames) {
	struct writer_raw * w = writer->w;
	return fwrite(buffer, sizeof(int16_t) * w->channels, frames, w->f);
}

static void writer_raw_close(struct writer * writer) {
	struct writer_raw * w = writer->w;
	writer->opened = false;
	if (w->f != NULL) {
		fclose(w->f);
		w->f = NULL;
	}
}

static void writer_raw_free(struct writer * writer) {
	if (writer == NULL)
		return;
	writer->close(writer);
	free(writer->w);
	free(writer);
}

struct writer * writer_raw_new(unsigned int channels) {

	struct writer * writer;
	if ((writer = malloc(sizeof(*writer))) == NULL)
		return NULL;

	writer->format = WRITER_FORMAT_RAW;
	writer->open = writer_raw_open;
	writer->write = writer_raw_write;
	writer->close = writer_raw_close;
	writer->free = writer_raw_free;

	struct writer_raw * w;
	if ((writer->w = w = calloc(1, sizeof(*w))) == NULL) {
		free(writer);
		return NULL;
	}

	w->channels = channels;

	return writer;
}

const char * writer_format_to_string(enum writer_format format) {
	switch (format) {
	case WRITER_FORMAT_RAW:
		return "raw";
#if ENABLE_SNDFILE
	case WRITER_FORMAT_WAV:
		return "wav";
#endif
#if ENABLE_MP3LAME
	case WRITER_FORMAT_MP3:
		return "mp3";
#endif
#if ENABLE_VORBIS
	case WRITER_FORMAT_OGG:
		return "ogg";
#endif
	}
	return "unknown";
}
