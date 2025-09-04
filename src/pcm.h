/*
 * SVAR - pcm.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_PCM_H_
#define SVAR_PCM_H_

#include <stddef.h>

/* Supported PCM formats. */
enum pcm_format {
	PCM_FORMAT_U8,
	PCM_FORMAT_S16LE,
};

const char * pcm_format_name(enum pcm_format format);
size_t pcm_format_size(enum pcm_format format, size_t samples);

double pcm_rms_db(enum pcm_format format, const void * buffer, size_t samples);

#endif
