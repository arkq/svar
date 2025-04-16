/*
 * SVAR - pcm.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include <endian.h>
#include <math.h>
#include <stdint.h>

#include "pcm.h"

const char * pcm_format_name(enum pcm_format format) {
	switch (format) {
	case PCM_FORMAT_U8:
		return "U8";
	case PCM_FORMAT_S16LE:
		return "S16LE";
	}
	return "UNKNOWN";
}

size_t pcm_format_size(enum pcm_format format, size_t samples) {
	switch (format) {
	case PCM_FORMAT_U8:
		return sizeof(uint8_t) * samples;
	case PCM_FORMAT_S16LE:
		return sizeof(int16_t) * samples;
	}
	return 0;
}

static double pcm_rms_u8(const uint8_t * buffer, size_t samples) {
	unsigned long sum2 = 0;
	for (size_t i = 0; i < samples; i++) {
		const int v = (int)buffer[i] - 0x80;
		sum2 += v * v;
	}
	return sqrt((double)sum2 / samples);
}

static double pcm_rms_s16le(const int16_t * buffer, size_t samples) {
	unsigned long long sum2 = 0;
	for (size_t i = 0; i < samples; i++) {
		const int16_t v = le16toh(buffer[i]);
		sum2 += (long)v * (long)v;
	}
	return sqrt((double)sum2 / samples);
}

/**
 * Calculate the root mean square (RMS) of given PCM data in dB. */
double pcm_rms_db(enum pcm_format format, const void * buffer, size_t samples) {

	unsigned int sample_value_max = 0;
	double rms = 0;
	double db = -96.0;

	if (samples > 0)
		switch (format) {
		case PCM_FORMAT_U8:
			rms = pcm_rms_u8(buffer, samples);
			sample_value_max = 0x7F;
			break;
		case PCM_FORMAT_S16LE:
			rms = pcm_rms_s16le(buffer, samples);
			sample_value_max = 0x7FFF;
			break;
		}

	if (rms > 0)
		db = 20 * log10(rms / sample_value_max);

	return db;
}
