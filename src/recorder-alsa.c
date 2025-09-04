/*
 * SVAR - recorder-alsa.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "recorder-alsa.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <alsa/asoundlib.h>

#include "log.h"
#include "pcm.h"
#include "rbuf.h"
#include "recorder.h"

static const snd_pcm_format_t pcm_format_mapping[] = {
	[PCM_FORMAT_U8] = SND_PCM_FORMAT_U8,
	[PCM_FORMAT_S16LE] = SND_PCM_FORMAT_S16_LE,
};

static void recorder_alsa_list(struct recorder * r) {
	(void)r;

	int err;
	void ** hints;
	if ((err = snd_device_name_hint(-1, "pcm", &hints)) != 0) {
		error("Couldn't get PCM device list: %s", snd_strerror(err));
		return;
	}

	for (void ** n = hints; *n != NULL; n++) {

		char * io;
		/* Get PCMs with capture capabilities. */
		if ((io = snd_device_name_get_hint(*n, "IOID")) != NULL &&
				strcmp(io, "Input") != 0) {
			free(io);
			continue;
		}

		char * name = snd_device_name_get_hint(*n, "NAME");
		char * desc = snd_device_name_get_hint(*n, "DESC");

		printf("%s\n", name);

		if (desc != NULL) {
			char * ss, * s = desc;
			while ((ss = strchr(s, '\n')) != NULL) {
				ss[0] = '\0';
				printf("    %s\n", s);
				s = &ss[1];
			}
			printf("    %s\n", s);
		}

		free(name);
		free(desc);
		free(io);

	}

	snd_device_name_free_hint(hints);

}

static void recorder_alsa_free(struct recorder * r) {
	snd_pcm_t * pcm = r->r;
	if (pcm != NULL)
		snd_pcm_close(pcm);
}

static int pcm_set_hw_params(snd_pcm_t * pcm, enum pcm_format format,
		unsigned int * channels, unsigned int * rate) {

	snd_pcm_hw_params_t *params;
	int dir = 0;
	int err;

	snd_pcm_hw_params_alloca(&params);

	if ((err = snd_pcm_hw_params_any(pcm, params)) < 0) {
		error("Couldn't set HW parameters: %s: %s", "Set all possible ranges", snd_strerror(err));
		return -1;
	}
	if ((err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED)) != 0) {
		error("Couldn't set HW parameters: %s: %s", "Set assess type", snd_strerror(err));
		return -1;
	}
	if ((err = snd_pcm_hw_params_set_format(pcm, params, pcm_format_mapping[format])) != 0) {
		error("Couldn't set HW parameters: %s: %s", "Set format", snd_strerror(err));
		return -1;
	}
	if ((err = snd_pcm_hw_params_set_channels_near(pcm, params, channels)) != 0) {
		error("Couldn't set HW parameters: %s: %s", "Set channels", snd_strerror(err));
		return -1;
	}
	if ((err = snd_pcm_hw_params_set_rate_near(pcm, params, rate, &dir)) != 0) {
		error("Couldn't set HW parameters: %s: %s", "Set sampling rate", snd_strerror(err));
		return -1;
	}
	if ((err = snd_pcm_hw_params(pcm, params)) != 0) {
		error("Couldn't set HW parameters: %s", snd_strerror(err));
		return -1;
	}

	return 0;
}

static int recorder_alsa_open(struct recorder * r, const char * device) {

	snd_pcm_t * pcm;
	int err;

	if ((err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0)) != 0) {
		error("Couldn't open PCM device: %s", snd_strerror(err));
		goto fail;
	}

	r->r = pcm;

	if (pcm_set_hw_params(pcm, r->format, &r->channels, &r->rate) != 0)
		goto fail;

	if ((err = snd_pcm_prepare(pcm)) != 0) {
		error("Couldn't prepare PCM: %s", snd_strerror(err));
		goto fail;
	}

	return 0;

fail:
	recorder_alsa_free(r);
	return -1;
}

/* Thread function for ALSA capture. */
static void *alsa_capture_thread(void * arg) {
	struct recorder * r = arg;
	snd_pcm_t * pcm = r->r;

	while (r->started) {

		pthread_mutex_lock(&r->mutex);
		const size_t samples = rbuf_write_linear_capacity(&r->rb);
		pthread_mutex_unlock(&r->mutex);

		const size_t pcm_read_frames = r->rate / 10;
		snd_pcm_sframes_t frames = samples / r->channels;
		if ((frames = snd_pcm_readi(pcm, r->rb.tail, MIN(frames, pcm_read_frames))) < 0)
			switch (frames) {
			case -EPIPE:
			case -ESTRPIPE:
				snd_pcm_recover(pcm, frames, 1);
				if (r->verbose >= 1)
					warn("PCM buffer overrun: %s", snd_strerror(frames));
				continue;
			case -ENODEV:
				error("PCM read error: %s", "Device disconnected");
				return NULL;
			default:
				error("PCM read error: %s", snd_strerror(frames));
				continue;
			}

		if (recorder_monitor(r, r->rb.tail, frames * r->channels) == 0) {
			pthread_mutex_lock(&r->mutex);
			rbuf_write_linear_commit(&r->rb, frames * r->channels);
			pthread_mutex_unlock(&r->mutex);
		}

		/* Wake up the processing thread to process audio or to close
		 * the current writer if split time was exceeded. */
		pthread_cond_signal(&r->cond);

	}

	return NULL;
}

static int recorder_alsa_start(struct recorder * r) {

	int err;
	pthread_t th;
	if ((err = pthread_create(&th, NULL, &alsa_capture_thread, r)) != 0) {
		error("Couldn't create ALSA capture thread: %s", strerror(-err));
		return -1;
	}

	/* Block until the capture thread exits. */
	pthread_join(th, NULL);

	return 0;
}

static void recorder_alsa_stop(struct recorder * r) {
	(void)r;
}

struct recorder * recorder_alsa_new(
		enum pcm_format format, unsigned int channels, unsigned int rate) {

	struct recorder * r;
	if ((r = recorder_new(format, channels, rate)) == NULL)
		return NULL;

	r->type = RECORDER_TYPE_ALSA;
	r->open = recorder_alsa_open;
	r->start = recorder_alsa_start;
	r->stop = recorder_alsa_stop;
	r->list = recorder_alsa_list;
	r->free = recorder_alsa_free;

	return r;
}
