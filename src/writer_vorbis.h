/*
 * SVAR - writer_vorbis.h
 * Copyright (c) 2010-2020 Arkadiusz Bokowy
 *
 * This file is a part of SVAR.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#ifndef SVAR_WRITER_VORBIS_H_
#define SVAR_WRITER_VORBIS_H_

#include <stdint.h>
#include <stdio.h>
#include <vorbis/vorbisenc.h>

struct writer_vorbis {
	ogg_stream_state ogg_s;
	ogg_packet ogg_p_main;
	ogg_packet ogg_p_comm;
	ogg_packet ogg_p_code;
	vorbis_info vbs_i;
	vorbis_dsp_state vbs_d;
	vorbis_block vbs_b;
	vorbis_comment vbs_c;
	FILE *fp;
};

struct writer_vorbis *writer_vorbis_init(int channels, int sampling,
		int bitrate_min, int bitrate_nom, int bitrate_max, const char *comment);
void writer_vorbis_free(struct writer_vorbis *w);

int writer_vorbis_open(struct writer_vorbis *w, const char *pathname);
void writer_vorbis_close(struct writer_vorbis *w);

ssize_t writer_vorbis_write(struct writer_vorbis *w, int16_t *buffer, size_t frames);

#endif
