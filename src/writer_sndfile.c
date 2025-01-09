/*
 * SVAR - writer_sndfile.c
 * SPDX-FileCopyrightText: 2010-2020 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer_sndfile.h"

#include <errno.h>
#include <stdlib.h>

#include "debug.h"

struct writer_sndfile *writer_sndfile_init(int channels, int sampling, int format) {

	struct writer_sndfile *w;
	if ((w = calloc(1, sizeof(*w))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	w->sfinfo.format = format;
	w->sfinfo.channels = channels;
	w->sfinfo.samplerate = sampling;

	return w;
}

void writer_sndfile_free(struct writer_sndfile *w) {
	writer_sndfile_close(w);
	free(w);
}

int writer_sndfile_open(struct writer_sndfile *w, const char *pathname) {

	writer_sndfile_close(w);

	if ((w->sf = sf_open(pathname, SFM_WRITE, &w->sfinfo)) == NULL) {
		error("Couldn't create output file: %s", sf_strerror(NULL));
		return -1;
	}

	return 0;
}

void writer_sndfile_close(struct writer_sndfile *w) {
	if (w->sf != NULL) {
		sf_close(w->sf);
		w->sf = NULL;
	}
}

ssize_t writer_sndfile_write(struct writer_sndfile *w, int16_t *buffer, size_t frames) {
	return sf_writef_short(w->sf, buffer, frames);
}
