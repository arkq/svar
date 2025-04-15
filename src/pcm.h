/*
 * SVAR - pcm.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_PCM_H_
#define SVAR_PCM_H_

#include <stddef.h>
#include <sys/types.h>

/* Supported PCM formats. */
enum pcm_format {
	PCM_FORMAT_S16LE,
};

const char * pcm_format_name(enum pcm_format format);
size_t pcm_format_size(enum pcm_format format, size_t samples);

#endif
