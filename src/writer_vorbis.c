/*
 * SVAR - writer_vorbis.c
 * SPDX-FileCopyrightText: 2010-2020 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer_vorbis.h"

#include <errno.h>
#include <stdlib.h>
#include <time.h>

static size_t do_analysis_and_write_ogg(struct writer_vorbis *w) {

	ogg_packet o_pack;
	ogg_page o_page;
	size_t len = 0;

	/* do main analysis and create packets */
	while (vorbis_analysis_blockout(&w->vbs_d, &w->vbs_b) == 1) {
		vorbis_analysis(&w->vbs_b, NULL);
		vorbis_bitrate_addblock(&w->vbs_b);

		while (vorbis_bitrate_flushpacket(&w->vbs_d, &o_pack)) {
			ogg_stream_packetin(&w->ogg_s, &o_pack);

			/* form OGG pages and write it to output file */
			while (ogg_stream_pageout(&w->ogg_s, &o_page)) {
				len += fwrite(o_page.header, 1, o_page.header_len, w->fp);
				len += fwrite(o_page.body, 1, o_page.body_len, w->fp);
			}
		}
	}

	return len;
}

struct writer_vorbis *writer_vorbis_init(int channels, int sampling,
		int bitrate_min, int bitrate_nom, int bitrate_max, const char *comment) {

	struct writer_vorbis *w;
	if ((w = calloc(1, sizeof(*w))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	vorbis_info_init(&w->vbs_i);
	switch (vorbis_encode_init(&w->vbs_i, channels, sampling,
				bitrate_max, bitrate_nom, bitrate_min)) {
	case 0:
		break;
	case OV_EINVAL:
		errno = EINVAL;
		goto fail;
	case OV_EIMPL:
		errno = ENOSYS;
		goto fail;
	default:
		errno = EFAULT;
		goto fail;
	}

	vorbis_comment_init(&w->vbs_c);
	vorbis_comment_add(&w->vbs_c, comment);

	return w;

fail:
	writer_vorbis_free(w);
	return NULL;
}

void writer_vorbis_free(struct writer_vorbis *w) {
	writer_vorbis_close(w);
	vorbis_comment_clear(&w->vbs_c);
	vorbis_info_clear(&w->vbs_i);
	free(w);
}

int writer_vorbis_open(struct writer_vorbis *w, const char *pathname) {

	writer_vorbis_close(w);

	if ((w->fp = fopen(pathname, "w")) == NULL)
		return -1;

	/* initialize vorbis analyzer */
	vorbis_analysis_init(&w->vbs_d, &w->vbs_i);
	vorbis_block_init(&w->vbs_d, &w->vbs_b);
	ogg_stream_init(&w->ogg_s, time(NULL));

	/* write header packets to the OGG stream */
	vorbis_analysis_headerout(&w->vbs_d, &w->vbs_c, &w->ogg_p_main, &w->ogg_p_comm, &w->ogg_p_code);
	ogg_stream_packetin(&w->ogg_s, &w->ogg_p_main);
	ogg_stream_packetin(&w->ogg_s, &w->ogg_p_comm);
	ogg_stream_packetin(&w->ogg_s, &w->ogg_p_code);

	return 0;
}

void writer_vorbis_close(struct writer_vorbis *w) {

	ogg_page o_page;
	size_t len = 0;

	if (w->fp == NULL)
		return;
	vorbis_analysis_wrote(&w->vbs_d, 0);
	do_analysis_and_write_ogg(w);
	/* flush any un-written partial ogg page */
	while (ogg_stream_flush(&w->ogg_s, &o_page)) {
		len += fwrite(o_page.header, 1, o_page.header_len, w->fp);
		len += fwrite(o_page.body, 1, o_page.body_len, w->fp);
	}
	ogg_stream_clear(&w->ogg_s);
	vorbis_block_clear(&w->vbs_b);
	vorbis_dsp_clear(&w->vbs_d);
	fclose(w->fp);
	w->fp = NULL;
}

ssize_t writer_vorbis_write(struct writer_vorbis *w, int16_t *buffer, size_t frames) {

	/* convert interleaved 16-bit buffer into vorbis buffer */
	float **vbs_buffer = vorbis_analysis_buffer(&w->vbs_d, frames);
	for (size_t fi = 0; fi < frames; fi++)
		for (int ci = 0; ci < w->vbs_i.channels; ci++)
			vbs_buffer[ci][fi] = (float)(buffer[fi * w->vbs_i.channels + ci]) / 0x7ffe;

	vorbis_analysis_wrote(&w->vbs_d, frames);
	return do_analysis_and_write_ogg(w);
}
