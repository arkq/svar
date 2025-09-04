/*
 * SVAR - recorder.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_RECORDER_H_
#define SVAR_RECORDER_H_

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "pcm.h"
#include "rbuf.h"
#include "writer.h"

enum recorder_type {
#if ENABLE_ALSA
	RECORDER_TYPE_ALSA,
#endif
#if ENABLE_PIPEWIRE
	RECORDER_TYPE_PIPEWIRE,
#endif
#if ENABLE_PORTAUDIO
	RECORDER_TYPE_PORTAUDIO,
#endif
};

struct recorder {

	enum recorder_type type;
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

	/* Implementation specific functions. */
	int (*open)(struct recorder * r, const char * device);
	int (*start)(struct recorder * r);
	void (*stop)(struct recorder * r);
	void (*list)(struct recorder * r);
	void (*free)(struct recorder * r);

	/* Implementation specific data. */
	void * r;

};

const char * recorder_type_to_string(enum recorder_type type);

struct recorder * recorder_new(
		enum pcm_format format,
		unsigned int channels,
		unsigned int rate);

void recorder_free(
		struct recorder * r);

void recorder_list_devices(
		struct recorder * r);

int recorder_open(
		struct recorder * r,
		const char * device);

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
