/*
 * SVAR - writer-mp3.h
 * SPDX-FileCopyrightText: 2010-2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_WRITER_MP3_H_
#define SVAR_WRITER_MP3_H_

#include "writer.h"

struct writer * writer_mp3_new(unsigned int channels, unsigned int sampling,
		int bitrate_min, int bitrate_max, const char * comment);
void writer_mp3_print_internals(const struct writer * writer);

#endif
