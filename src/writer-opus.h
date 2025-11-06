/*
 * SVAR - writer-opus.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_WRITER_OPUS_H_
#define SVAR_WRITER_OPUS_H_

#include "pcm.h"
#include "writer.h"

struct writer * writer_opus_new(
		enum pcm_format format, unsigned int channels, unsigned int sampling,
		int bitrate, const char * comment);

#endif
