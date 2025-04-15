/*
 * SVAR - pcm.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "pcm.h"

const char * pcm_format_name(enum pcm_format format) {
	switch (format) {
	case PCM_FORMAT_S16LE:
		return "S16LE";
	}
	return "UNKNOWN";
}

size_t pcm_format_size(enum pcm_format format, size_t samples) {
	switch (format) {
	case PCM_FORMAT_S16LE:
		return sizeof(int16_t) * samples;
	}
	return 0;
}
