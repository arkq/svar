/*
 * SVAR - writer-opus.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer-opus.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <opusenc.h>

#include "log.h"
#include "pcm.h"
#include "writer.h"

struct writer_opus {
	OggOpusEnc * enc;
	OggOpusComments * comments;
	unsigned int channels;
	unsigned int sampling;
	int bitrate;
};

static int writer_opus_open(struct writer * writer, const char * pathname) {
	struct writer_opus * w = writer->w;

	writer->close(writer);

	int err;
	const int family = w->channels <= 2 ? 0 : 1;
	if ((w->enc = ope_encoder_create_file(pathname, w->comments, w->sampling,
				w->channels, family, &err)) == NULL) {
		error("OPUS: Couldn't create encoder: %s", ope_strerror(err));
		errno = EINVAL;
		return -1;
	}

	if ((err = ope_encoder_ctl(w->enc, OPUS_SET_BITRATE(w->bitrate))) != OPE_OK)
		warn("OPUS: Couldn't set bitrate: %s", ope_strerror(err));

	writer->opened = true;
	return 0;
}

static void writer_opus_close(struct writer * writer) {
	struct writer_opus * w = writer->w;
	writer->opened = false;

	if (w->enc == NULL)
		return;

	ope_encoder_drain(w->enc);
	ope_encoder_destroy(w->enc);
	w->enc = NULL;
}

static ssize_t writer_opus_write(struct writer * writer, const void * buffer, size_t frames) {
	struct writer_opus * w = writer->w;

	int err;
	/* Write 16-bit PCM samples to the encoder. */
	if ((err = ope_encoder_write(w->enc, (const opus_int16 *)buffer, frames)) != OPE_OK) {
		error("OPUS: Encoding error: %s", ope_strerror(err));
		return -1;
	}

	return frames;
}

static void writer_opus_free(struct writer * writer) {
	if (writer == NULL)
		return;
	struct writer_opus * w = writer->w;
	writer->close(writer);
	if (w->comments != NULL)
		ope_comments_destroy(w->comments);
	free(writer->w);
	free(writer);
}

struct writer * writer_opus_new(
		enum pcm_format format, unsigned int channels, unsigned int sampling,
		int bitrate, const char * comment) {

	if (format != PCM_FORMAT_S16LE) {
		error("OPUS: Unsupported PCM format: %s", pcm_format_name(format));
		return NULL;
	}

	struct writer * writer;
	if ((writer = malloc(sizeof(*writer))) == NULL)
		return NULL;

	writer->type = WRITER_TYPE_OPUS;
	writer->opened = false;
	writer->open = writer_opus_open;
	writer->write = writer_opus_write;
	writer->close = writer_opus_close;
	writer->free = writer_opus_free;

	struct writer_opus * w;
	if ((writer->w = w = calloc(1, sizeof(*w))) == NULL) {
		free(writer);
		return NULL;
	}

	if ((w->comments = ope_comments_create()) == NULL) {
		writer_opus_free(writer);
		return NULL;
	}

	w->channels = channels;
	w->sampling = sampling;
	w->bitrate = bitrate;

	if (comment != NULL) {
		char tag[8 + strlen(comment) + 1];
		snprintf(tag, sizeof(tag), "ENCODER=%s", comment);
		ope_comments_add_string(w->comments, tag);
	}

	return writer;
}
