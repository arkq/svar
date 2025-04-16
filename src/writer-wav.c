/*
 * SVAR - writer-wav.c
 * SPDX-FileCopyrightText: 2010-2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer-wav.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <sndfile.h>

#include "log.h"
#include "writer.h"

struct writer_wav {
	SNDFILE * sf;
	SF_INFO sfinfo;
};

static int writer_wav_open(struct writer * writer, const char * pathname) {
	struct writer_wav * w = writer->w;

	writer->close(writer);
	if ((w->sf = sf_open(pathname, SFM_WRITE, &w->sfinfo)) == NULL) {
		error("Couldn't create output file: %s", sf_strerror(NULL));
		return -1;
	}

	writer->opened = true;
	return 0;
}

static void writer_wav_close(struct writer * writer) {
	struct writer_wav * w = writer->w;
	writer->opened = false;
	if (w->sf != NULL) {
		sf_close(w->sf);
		w->sf = NULL;
	}
}

static ssize_t writer_wav_write_raw(struct writer * writer, const void * buffer, size_t frames) {
	struct writer_wav * w = writer->w;
	return sf_write_raw(w->sf, buffer, frames * w->sfinfo.channels);
}

static ssize_t writer_wav_write_short(struct writer * writer, const void * buffer, size_t frames) {
	struct writer_wav * w = writer->w;
	return sf_writef_short(w->sf, buffer, frames);
}

static void writer_wav_free(struct writer * writer) {
	if (writer == NULL)
		return;
	writer->close(writer);
	free(writer->w);
	free(writer);
}

struct writer * writer_wav_new(
		enum pcm_format format, unsigned int channels, unsigned int sampling) {

	struct writer * writer;
	if ((writer = malloc(sizeof(*writer))) == NULL)
		return NULL;

	writer->type = WRITER_TYPE_WAV;
	writer->opened = false;
	writer->open = writer_wav_open;
	writer->write = writer_wav_write_raw;
	if (format == PCM_FORMAT_S16LE)
		writer->write = writer_wav_write_short;
	writer->close = writer_wav_close;
	writer->free = writer_wav_free;

	struct writer_wav * w;
	if ((writer->w = w = calloc(1, sizeof(*w))) == NULL) {
		free(writer);
		return NULL;
	}

	static const int format2sf[] = {
		[PCM_FORMAT_U8] = SF_FORMAT_PCM_U8,
		[PCM_FORMAT_S16LE] = SF_FORMAT_PCM_16,
	};

	w->sfinfo.format = SF_FORMAT_WAV | format2sf[format];
	w->sfinfo.channels = channels;
	w->sfinfo.samplerate = sampling;

	return writer;
}
