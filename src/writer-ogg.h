/*
 * SVAR - writer-ogg.h
 * SPDX-FileCopyrightText: 2010-2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_WRITER_OGG_H_
#define SVAR_WRITER_OGG_H_

#include "writer.h"

struct writer * writer_ogg_new(unsigned int channels, unsigned int sampling,
		int bitrate_min, int bitrate_nom, int bitrate_max, const char * comment);

#endif
