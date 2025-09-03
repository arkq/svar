/*
 * SVAR - recorder.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_RECORDER_H_
#define SVAR_RECORDER_H_

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "pcm.h"
#include "rbuf.h"
#include "writer.h"

struct recorder {

	enum pcm_format format;
	unsigned int channels;
	unsigned int rate;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
	/* Ring buffer for audio data. */
	struct rbuf rb;

	bool started;
	/* Pointer to the associated writer. */
	struct writer * w;

	char * output_file_template;
	double activation_threshold_level_db;
	long activation_fadeout_time_ms;
	long output_split_time_ms;

	/* Run recorder in the monitor mode. */
	bool monitor;
	/* Logging verbosity level. */
	int verbose;

};

struct recorder * recorder_new(
		enum pcm_format format,
		unsigned int channels,
		unsigned int rate);

void recorder_free(
		struct recorder * r);

int recorder_start(
		struct recorder * r,
		struct writer * w,
		const char * output_file_template,
		double activation_threshold_level_db,
		long activation_fadeout_time_ms,
		long output_split_time_ms);

int recorder_stop(
		struct recorder * r);

int recorder_monitor(
		struct recorder * r,
		const void * buffer,
		size_t samples);

int recorder_process(
		struct recorder * r,
		const void * buffer,
		size_t samples);

#endif
