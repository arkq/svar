/*
 * SVAR - writer-vorbis.h
 * SPDX-FileCopyrightText: 2010-2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_WRITER_VORBIS_H_
#define SVAR_WRITER_VORBIS_H_

#include "pcm.h"
#include "writer.h"

struct writer * writer_vorbis_new(
		enum pcm_format format, unsigned int channels, unsigned int sampling,
		int bitrate_min, int bitrate_nom, int bitrate_max, const char * comment);

#endif
