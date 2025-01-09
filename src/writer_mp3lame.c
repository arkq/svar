/*
 * SVAR - writer_mp3lame.c
 * SPDX-FileCopyrightText: 2010-2020 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer_mp3lame.h"

#include <errno.h>
#include <stdlib.h>

#include "debug.h"

static int lame_encode(lame_global_flags *gfp, short int *buffer, int samples,
		unsigned char *mp3buf, int size) {
	if (lame_get_num_channels(gfp) == 1)
		return lame_encode_buffer(gfp, buffer, NULL, samples, mp3buf, size);
	return lame_encode_buffer_interleaved(gfp, buffer, samples, mp3buf, size);
}

struct writer_mp3lame *writer_mp3lame_init(int channels, int sampling,
		int bitrate_min, int bitrate_max, const char *comment) {

	struct writer_mp3lame *w;
	if ((w = calloc(1, sizeof(*w))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	if ((w->gfp = lame_init()) == NULL)
		goto fail;

	if (lame_set_num_channels(w->gfp, channels) != 0) {
		error("LAME: Unsupported number of channels: %d", channels);
		errno = EINVAL;
		goto fail;
	}

	if (lame_set_in_samplerate(w->gfp, sampling) != 0) {
		error("LAME: Unsupported sampling rate: %d", sampling);
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

	return w;

fail:
	writer_mp3lame_free(w);
	return NULL;
}

void writer_mp3lame_free(struct writer_mp3lame *w) {
	writer_mp3lame_close(w);
	if (w->gfp != NULL)
		lame_close(w->gfp);
	free(w);
}

int writer_mp3lame_open(struct writer_mp3lame *w, const char *pathname) {

	writer_mp3lame_close(w);

	if ((w->fp = fopen(pathname, "w")) == NULL)
		return -1;

	int len = lame_get_id3v2_tag(w->gfp, w->mp3buf, sizeof(w->mp3buf));
	fwrite(w->mp3buf, 1, len, w->fp);

	return 0;
}

void writer_mp3lame_close(struct writer_mp3lame *w) {
	if (w->fp == NULL)
		return;
	int len = lame_encode_flush(w->gfp, w->mp3buf, sizeof(w->mp3buf));
	fwrite(w->mp3buf, 1, len, w->fp);
	fclose(w->fp);
	w->fp = NULL;
}

ssize_t writer_mp3lame_write(struct writer_mp3lame *w, int16_t *buffer, size_t frames) {
	int len = lame_encode(w->gfp, buffer, frames, w->mp3buf, sizeof(w->mp3buf));
	return fwrite(w->mp3buf, 1, len, w->fp);
}
