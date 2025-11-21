/*
 * SVAR - recorder.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE

#include "recorder.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>

#include "log.h"
#include "pcm.h"
#include "rbuf.h"
#include "writer.h"

static time_t ts_diff_ms(const struct timespec * ts1, const struct timespec * ts2) {
	return (ts1->tv_sec - ts2->tv_sec) * 1000 + (ts1->tv_nsec - ts2->tv_nsec) / 1000000;
}

const char * recorder_type_to_string(enum recorder_type type) {
	switch (type) {
#if ENABLE_ALSA
	case RECORDER_TYPE_ALSA:
		return "ALSA";
#endif
#if ENABLE_PIPEWIRE
	case RECORDER_TYPE_PIPEWIRE:
		return "PipeWire";
#endif
#if ENABLE_PORTAUDIO
	case RECORDER_TYPE_PORTAUDIO:
		return "PortAudio";
#endif
	}
	return "unknown";
}

struct recorder * recorder_new(
		enum pcm_format format,
		unsigned int channels,
		unsigned int rate) {

	struct recorder * r;
	if ((r = calloc(1, sizeof(*r))) == NULL)
		return NULL;

	pthread_mutex_init(&r->mutex, NULL);
	pthread_cond_init(&r->cond, NULL);

	const size_t pcm_read_frames = rate / 10;
	const size_t rb_nmemb = channels * pcm_read_frames * 8;
	if (rbuf_init(&r->rb, rb_nmemb, pcm_format_size(format, 1)) != 0)
		goto fail;

	r->format = format;
	r->channels = channels;
	r->rate = rate;

	return r;

fail:
	recorder_free(r);
	return NULL;
}

void recorder_free(
		struct recorder * r) {
	if (r->free != NULL)
		r->free(r);
	pthread_mutex_destroy(&r->mutex);
	pthread_cond_destroy(&r->cond);
	free(r->output_file_template);
	rbuf_free(&r->rb);
	free(r);
}

void recorder_list_devices(
		struct recorder * r) {
	r->list(r);
}

int recorder_open(
		struct recorder * r,
		const char * device) {
	return r->open(r, device);
}

static void * recorder_thread(void * arg) {
	struct recorder * r = arg;
	struct writer * w = r->w;

	if (r->monitor)
		return NULL;

	struct timespec ts_last_write = { 0 };
	struct timespec ts_now;
	size_t samples = 0;

	while (true) {

		pthread_mutex_lock(&r->mutex);

		while (r->started && (samples = rbuf_read_linear_capacity(&r->rb)) == 0) {
			/* Wait for the reader to fill the buffer. */
			pthread_cond_wait(&r->cond, &r->mutex);
			/* If split time is enabled and writer is opened,
			 * check if we need to close the current writer. */
			if (r->output_split_time_ms && w->opened) {
				clock_gettime(CLOCK_MONOTONIC_RAW, &ts_now);
				if (ts_diff_ms(&ts_now, &ts_last_write) > r->output_split_time_ms) {
					if (r->verbose >= 1)
						info("Closing current output file");
					w->close(w);
				}
			}
		}

		pthread_mutex_unlock(&r->mutex);

		if (!r->started)
			break;

		if (!w->opened) {

			struct tm now;
			const time_t tmp = time(NULL);
			localtime_r(&tmp, &now);

			char name[PATH_MAX];
			size_t n = strftime(name, sizeof(name), r->output_file_template, &now);
			snprintf(&name[n], sizeof(name) - n, ".%s", writer_type_to_extension(w->type));

			if (r->verbose >= 1)
				info("Creating new output file: %s", name);

			if (w->open(w, name) == -1) {
				error("Couldn't open writer: %s", strerror(errno));
				goto fail;
			}

		}

		clock_gettime(CLOCK_MONOTONIC_RAW, &ts_last_write);
		w->write(w, r->rb.head, samples / r->channels);

		pthread_mutex_lock(&r->mutex);
		rbuf_read_linear_commit(&r->rb, samples);
		pthread_mutex_unlock(&r->mutex);

	}

fail:
	if (w->opened && r->verbose >= 1)
		info("Closing current output file");
	w->free(w);
	return NULL;
}

int recorder_start(
		struct recorder * r,
		struct writer * w,
		const char * output_file_template,
		double activation_threshold_level_db,
		long activation_fadeout_time_ms,
		long output_split_time_ms) {

	r->started = true;
	r->output_file_template = strdup(output_file_template);
	r->activation_threshold_level_db = activation_threshold_level_db;
	r->activation_fadeout_time_ms = activation_fadeout_time_ms;
	r->output_split_time_ms = output_split_time_ms;
	r->w = w;

	int err;
	pthread_t th;
	/* Initialize thread for data processing. */
	if ((err = pthread_create(&th, NULL, &recorder_thread, r)) != 0)
		return errno = err, -1;

	/* Start the recorder backend. */
	r->start(r);

	pthread_mutex_lock(&r->mutex);
	r->started = false;
	pthread_mutex_unlock(&r->mutex);
	pthread_cond_signal(&r->cond);

	pthread_join(th, NULL);

	if (r->monitor)
		printf("\n");

	return 0;
}

int recorder_stop(
		struct recorder * r) {
	r->started = false;
	r->stop(r);
	return 0;
}

int recorder_monitor(
		struct recorder * r,
		const void * buffer,
		size_t samples) {

	static struct timespec activation_time = { 0 };
	double buffer_rms_db = pcm_rms_db(r->format, buffer, samples);

	if (r->monitor) {
		/* Dump current RMS values to the stdout. */
		printf("\rSignal RMS: %#5.1f dB\r", buffer_rms_db);
		fflush(stdout);
		return -2;
	}

	/* Update time if the signal RMS in the buffer exceeds the threshold. */
	if (buffer_rms_db > r->activation_threshold_level_db)
		clock_gettime(CLOCK_MONOTONIC_RAW, &activation_time);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	if (ts_diff_ms(&now, &activation_time) <= r->activation_fadeout_time_ms)
		return 0;

	return -1;
}

int recorder_process(
		struct recorder * r,
		const void * buffer,
		size_t samples) {

	if (recorder_monitor(r, buffer, samples) != 0)
		return 0;

	while (samples > 0) {

		pthread_mutex_lock(&r->mutex);

		const size_t rb_wr_capacity_samples = rbuf_write_linear_capacity(&r->rb);
		const size_t batch_samples = MIN(samples, rb_wr_capacity_samples);
		const size_t batch_sample_bytes = pcm_format_size(r->format, batch_samples);

		if (rb_wr_capacity_samples == 0) {
			if (r->verbose >= 1)
				warn("PCM buffer overrun: %s", "Ring buffer full");
			pthread_mutex_unlock(&r->mutex);
			break;
		}

		memcpy(r->rb.tail, buffer, batch_sample_bytes);
		rbuf_write_linear_commit(&r->rb, batch_samples);

		buffer = (const unsigned char *)buffer + batch_sample_bytes;
		samples -= batch_samples;

		pthread_mutex_unlock(&r->mutex);

		/* Wake up the processing thread to process audio or to close
		 * the current writer if split time was exceeded. */
		pthread_cond_signal(&r->cond);

	}

	return 0;
}
