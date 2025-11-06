/*
 * SVAR - writer-vorbis.c
 * SPDX-FileCopyrightText: 2010-2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "writer-vorbis.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include "log.h"
#include "pcm.h"
#include "writer.h"

struct writer_vorbis {
	ogg_stream_state ogg_s;
	ogg_packet ogg_p_main;
	ogg_packet ogg_p_comm;
	ogg_packet ogg_p_code;
	vorbis_info vbs_i;
	vorbis_dsp_state vbs_d;
	vorbis_block vbs_b;
	vorbis_comment vbs_c;
	FILE * fp;
};

static size_t do_analysis_and_write_ogg(struct writer_vorbis * w) {

	ogg_packet o_pack;
	ogg_page o_page;
	size_t len = 0;

	/* Do main analysis and create packets. */
	while (vorbis_analysis_blockout(&w->vbs_d, &w->vbs_b) == 1) {
		vorbis_analysis(&w->vbs_b, NULL);
		vorbis_bitrate_addblock(&w->vbs_b);

		while (vorbis_bitrate_flushpacket(&w->vbs_d, &o_pack)) {
			ogg_stream_packetin(&w->ogg_s, &o_pack);

			/* Form OGG pages and write it to output file. */
			while (ogg_stream_pageout(&w->ogg_s, &o_page)) {
				len += fwrite(o_page.header, 1, o_page.header_len, w->fp);
				len += fwrite(o_page.body, 1, o_page.body_len, w->fp);
			}
		}
	}

	return len;
}

static int writer_vorbis_open(struct writer * writer, const char * pathname) {
	struct writer_vorbis * w = writer->w;

	writer->close(writer);
	if ((w->fp = fopen(pathname, "w")) == NULL)
		return -1;

	/* Initialize vorbis analyzer. */
	vorbis_analysis_init(&w->vbs_d, &w->vbs_i);
	vorbis_block_init(&w->vbs_d, &w->vbs_b);
	ogg_stream_init(&w->ogg_s, time(NULL));

	/* Write header packets to the OGG stream. */
	vorbis_analysis_headerout(&w->vbs_d, &w->vbs_c, &w->ogg_p_main, &w->ogg_p_comm, &w->ogg_p_code);
	ogg_stream_packetin(&w->ogg_s, &w->ogg_p_main);
	ogg_stream_packetin(&w->ogg_s, &w->ogg_p_comm);
	ogg_stream_packetin(&w->ogg_s, &w->ogg_p_code);

	writer->opened = true;
	return 0;
}

static void writer_vorbis_close(struct writer * writer) {
	struct writer_vorbis * w = writer->w;
	writer->opened = false;

	ogg_page o_page;
	size_t len = 0;

	if (w->fp == NULL)
		return;
	vorbis_analysis_wrote(&w->vbs_d, 0);
	do_analysis_and_write_ogg(w);
	/* Flush any un-written partial OGG page. */
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

static ssize_t writer_vorbis_write(struct writer * writer, const void * buffer, size_t frames) {
	struct writer_vorbis * w = writer->w;

	/* Convert interleaved 16-bit buffer into vorbis buffer. */
	float ** vbs_buffer = vorbis_analysis_buffer(&w->vbs_d, frames);
	for (size_t fi = 0; fi < frames; fi++)
		for (int ci = 0; ci < w->vbs_i.channels; ci++)
			vbs_buffer[ci][fi] = (float)(((int16_t *)buffer)[fi * w->vbs_i.channels + ci]) / 0x7ffe;

	vorbis_analysis_wrote(&w->vbs_d, frames);
	return do_analysis_and_write_ogg(w);
}

static void writer_vorbis_free(struct writer * writer) {
	if (writer == NULL)
		return;
	struct writer_vorbis * w = writer->w;
	writer->close(writer);
	vorbis_comment_clear(&w->vbs_c);
	vorbis_info_clear(&w->vbs_i);
	free(writer->w);
	free(writer);
}

struct writer * writer_vorbis_new(
		enum pcm_format format, unsigned int channels, unsigned int sampling,
		int bitrate_min, int bitrate_nom, int bitrate_max, const char * comment) {

	if (format != PCM_FORMAT_S16LE) {
		error("VORBIS: Unsupported PCM format: %s", pcm_format_name(format));
		return NULL;
	}

	struct writer * writer;
	if ((writer = malloc(sizeof(*writer))) == NULL)
		return NULL;

	writer->type = WRITER_TYPE_VORBIS;
	writer->opened = false;
	writer->open = writer_vorbis_open;
	writer->write = writer_vorbis_write;
	writer->close = writer_vorbis_close;
	writer->free = writer_vorbis_free;

	struct writer_vorbis * w;
	if ((writer->w = w = calloc(1, sizeof(*w))) == NULL) {
		free(writer);
		return NULL;
	}

	vorbis_info_init(&w->vbs_i);
	vorbis_comment_init(&w->vbs_c);

	if (comment != NULL) {
		char tag[8 + strlen(comment) + 1];
		snprintf(tag, sizeof(tag), "ENCODER=%s", comment);
		vorbis_comment_add(&w->vbs_c, tag);
	}

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

	return writer;

fail:
	writer_vorbis_free(writer);
	return NULL;
}
