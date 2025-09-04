/*
 * SVAR - writer.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer.h"

#include <stdio.h>
#include <stdlib.h>

#include "pcm.h"

struct writer_raw {
	size_t frame_size;
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

static ssize_t writer_raw_write(struct writer * writer, const void * buffer, size_t frames) {
	struct writer_raw * w = writer->w;
	return fwrite(buffer, w->frame_size, frames, w->f);
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

struct writer * writer_raw_new(enum pcm_format format, unsigned int channels) {

	struct writer * writer;
	if ((writer = malloc(sizeof(*writer))) == NULL)
		return NULL;

	writer->type = WRITER_TYPE_RAW;
	writer->opened = false;
	writer->open = writer_raw_open;
	writer->write = writer_raw_write;
	writer->close = writer_raw_close;
	writer->free = writer_raw_free;

	struct writer_raw * w;
	if ((writer->w = w = calloc(1, sizeof(*w))) == NULL) {
		free(writer);
		return NULL;
	}

	w->frame_size = pcm_format_size(format, channels);

	return writer;
}

const char * writer_type_to_string(enum writer_type type) {
	switch (type) {
	case WRITER_TYPE_RAW:
		return "raw";
#if ENABLE_SNDFILE
	case WRITER_TYPE_WAV:
	case WRITER_TYPE_RF64:
		return "wav";
#endif
#if ENABLE_MP3LAME
	case WRITER_TYPE_MP3:
		return "mp3";
#endif
#if ENABLE_VORBIS
	case WRITER_TYPE_OGG:
		return "ogg";
#endif
	}
	return "unknown";
}
