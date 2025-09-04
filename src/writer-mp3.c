/*
 * SVAR - writer-mp3.c
 * SPDX-FileCopyrightText: 2010-2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer-mp3.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <lame/lame.h>

#include "log.h"
#include "pcm.h"
#include "writer.h"

struct writer_mp3 {
	lame_global_flags * gfp;
	unsigned char mp3buf[1024 * 64];
	FILE * fp;
};

static int lame_encode(lame_global_flags *gfp, const short int *buffer, int samples,
		unsigned char *mp3buf, int size) {
	if (lame_get_num_channels(gfp) == 1)
		return lame_encode_buffer(gfp, buffer, NULL, samples, mp3buf, size);
	return lame_encode_buffer_interleaved(gfp, (short *)buffer, samples, mp3buf, size);
}

static int writer_mp3_open(struct writer * writer, const char * pathname) {
	struct writer_mp3 * w = writer->w;

	writer->close(writer);
	if ((w->fp = fopen(pathname, "w")) == NULL)
		return -1;

	int len = lame_get_id3v2_tag(w->gfp, w->mp3buf, sizeof(w->mp3buf));
	fwrite(w->mp3buf, 1, len, w->fp);

	writer->opened = true;
	return 0;
}

static ssize_t writer_mp3_write(struct writer * writer, const void * buffer, size_t frames) {
	struct writer_mp3 * w = writer->w;
	int len = lame_encode(w->gfp, buffer, frames, w->mp3buf, sizeof(w->mp3buf));
	return fwrite(w->mp3buf, 1, len, w->fp);
}

static void writer_mp3_close(struct writer * writer) {
	struct writer_mp3 * w = writer->w;
	writer->opened = false;
	if (w->fp == NULL)
		return;
	int len = lame_encode_flush(w->gfp, w->mp3buf, sizeof(w->mp3buf));
	fwrite(w->mp3buf, 1, len, w->fp);
	fclose(w->fp);
	w->fp = NULL;
}

static void writer_mp3_free(struct writer * writer) {
	if (writer == NULL)
		return;
	struct writer_mp3 * w = writer->w;
	writer->close(writer);
	if (w->gfp != NULL)
		lame_close(w->gfp);
	free(writer->w);
	free(writer);
}

struct writer * writer_mp3_new(
		enum pcm_format format, unsigned int channels, unsigned int sampling,
		int bitrate_min, int bitrate_max, const char * comment) {

	if (format != PCM_FORMAT_S16LE) {
		error("MP3 unsupported PCM format: %s", pcm_format_name(format));
		return NULL;
	}

	struct writer * writer;
	if ((writer = malloc(sizeof(*writer))) == NULL)
		return NULL;

	writer->type = WRITER_TYPE_MP3;
	writer->opened = false;
	writer->open = writer_mp3_open;
	writer->write = writer_mp3_write;
	writer->close = writer_mp3_close;
	writer->free = writer_mp3_free;

	struct writer_mp3 * w;
	if ((writer->w = w = calloc(1, sizeof(*w))) == NULL) {
		free(writer);
		return NULL;
	}

	if ((w->gfp = lame_init()) == NULL)
		goto fail;

	if (lame_set_num_channels(w->gfp, channels) != 0) {
		error("LAME: Unsupported number of channels: %u", channels);
		errno = EINVAL;
		goto fail;
	}

	if (lame_set_in_samplerate(w->gfp, sampling) != 0) {
		error("LAME: Unsupported sampling rate: %u", sampling);
		errno = EINVAL;
		goto fail;
	}

	lame_set_VBR(w->gfp, vbr_default);
	lame_set_VBR_min_bitrate_kbps(w->gfp, bitrate_min);
	lame_set_VBR_max_bitrate_kbps(w->gfp, bitrate_max);
	lame_set_write_id3tag_automatic(w->gfp, 0);

	if (lame_init_params(w->gfp) != 0) {
		error("LAME: Couldn't setup encoder");
		goto fail;
	}

	id3tag_init(w->gfp);
	id3tag_set_comment(w->gfp, comment);

	return writer;

fail:
	writer_mp3_free(writer);
	return NULL;
}

void writer_mp3_print_internals(const struct writer * writer) {
	if (writer == NULL)
		return;
	const struct writer_mp3 * w = writer->w;
	lame_print_internals(w->gfp);
}
