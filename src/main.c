/*
 * SVAR - main.c
 * Copyright (c) 2010-2020 Arkadiusz Bokowy
 *
 * This file is a part of SVAR.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include <alsa/asoundlib.h>
#if ENABLE_MP3LAME
# include "writer_mp3lame.h"
#endif
#if ENABLE_SNDFILE
# include "writer_sndfile.h"
#endif
#if ENABLE_VORBIS
# include "writer_vorbis.h"
#endif

#include "debug.h"

#define READER_FRAMES 512 * 8
#define PROCESSING_FRAMES READER_FRAMES * 16

enum output_format {
	FORMAT_RAW = 0,
	FORMAT_WAV,
	FORMAT_MP3,
	FORMAT_OGG,
};

/* available output formats */
static const struct {
	enum output_format format;
	const char *name;
} output_formats[] = {
	{ FORMAT_RAW, "raw" },
#if ENABLE_MP3LAME
	{ FORMAT_MP3, "mp3" },
#endif
#if ENABLE_SNDFILE
	{ FORMAT_WAV, "wav" },
#endif
#if ENABLE_VORBIS
	{ FORMAT_OGG, "ogg" },
#endif
};

/* global application settings */
static struct appconfig_t {

	/* application banner */
	char *banner;

	/* capturing PCM device */
	snd_pcm_t *pcm;
	char pcm_device[25];
	unsigned int pcm_channels;
	unsigned int pcm_rate;

	/* if true, run signal meter only */
	bool signal_meter;
	/* output verboseness level */
	int verbose;

	char output_prefix[128];
	enum output_format output_format;

	int threshold;    /* % of max signal */
	int fadeout_time; /* in ms */
	int split_time;   /* in s (0 disables split) */

	/* variable bit rate settings for encoder (bit per second) */
	int bitrate_min;
	int bitrate_nom;
	int bitrate_max;

	/* read/write synchronization */
	pthread_mutex_t mutex;
	pthread_cond_t ready;
	int16_t *buffer;
	/* current position */
	size_t current;
	/* buffer size */
	size_t size;

} appconfig = {

	.banner = "SVAR - Simple Voice Activated Recorder",

	.pcm_device = "default",
	.pcm_channels = 1,
	.pcm_rate = 44100,

	.signal_meter = false,
	.verbose = 0,

	.output_prefix = "rec",
	/* default output format */
#if ENABLE_SNDFILE
	.output_format = FORMAT_WAV,
#elif ENABLE_VORBIS
	.output_format = FORMAT_OGG,
#elif ENABLE_MP3LAME
	.output_format = FORMAT_MP3,
#else
	.output_format = FORMAT_RAW,
#endif

	.threshold = 2,
	.fadeout_time = 500,
	.split_time = 0,

	/* default compression settings */
	.bitrate_min = 32000,
	.bitrate_nom = 64000,
	.bitrate_max = 128000,

};

static bool main_loop_on = true;
static void main_loop_stop(int sig) {
	/* Call to this handler restores the default action, so on the
	 * second call the program will be forcefully terminated. */

	struct sigaction sigact = { .sa_handler = SIG_DFL };
	sigaction(sig, &sigact, NULL);

	main_loop_on = false;
}

/* Set hardware parameters. */
static int set_hw_params(snd_pcm_t *pcm, char **msg) {

	snd_pcm_hw_params_t *params;
	char buf[256];
	int dir = 0;
	int err;

	snd_pcm_hw_params_alloca(&params);

	if ((err = snd_pcm_hw_params_any(pcm, params)) != 0) {
		snprintf(buf, sizeof(buf), "Set all possible ranges: %s", snd_strerror(err));
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED)) != 0) {
		snprintf(buf, sizeof(buf), "Set assess type: %s: %s",
				snd_strerror(err), snd_pcm_access_name(SND_PCM_ACCESS_RW_INTERLEAVED));
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE)) != 0) {
		snprintf(buf, sizeof(buf), "Set format: %s: %s",
				snd_strerror(err), snd_pcm_format_name(SND_PCM_FORMAT_S16_LE));
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_channels_near(pcm, params, &appconfig.pcm_channels)) != 0) {
		snprintf(buf, sizeof(buf), "Set channels: %s: %d", snd_strerror(err), appconfig.pcm_channels);
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_rate_near(pcm, params, &appconfig.pcm_rate, &dir)) != 0) {
		snprintf(buf, sizeof(buf), "Set sampling rate: %s: %d", snd_strerror(err), appconfig.pcm_rate);
		goto fail;
	}
	if ((err = snd_pcm_hw_params(pcm, params)) != 0) {
		snprintf(buf, sizeof(buf), "%s", snd_strerror(err));
		goto fail;
	}

	return 0;

fail:
	if (msg != NULL)
		*msg = strdup(buf);
	return err;
}

/* Return the name of a given output format. */
static const char *get_output_format_name(enum output_format format) {
	size_t i;
	for (i = 0; i < sizeof(output_formats) / sizeof(*output_formats); i++)
		if (output_formats[i].format == format)
			return output_formats[i].name;
	return NULL;
}

/* Print some information about the audio device and its configuration. */
static void print_audio_info(void) {
	printf("Selected PCM device: %s\n"
			"Hardware parameters: %iHz, %s, %i channel%s\n",
			appconfig.pcm_device,
			appconfig.pcm_rate,
			snd_pcm_format_name(SND_PCM_FORMAT_S16_LE),
			appconfig.pcm_channels, appconfig.pcm_channels > 1 ? "s" : "");
	if (!appconfig.signal_meter)
		printf("Output file format: %s\n",
				get_output_format_name(appconfig.output_format));
	if (appconfig.output_format == FORMAT_MP3)
		printf("Output bit rate [min, max]: %d, %d kbit/s\n",
				appconfig.bitrate_min / 1000,
				appconfig.bitrate_max / 1000);
	if (appconfig.output_format == FORMAT_OGG)
		printf("Output bit rate [min, nominal, max]: %d, %d, %d kbit/s\n",
				appconfig.bitrate_min / 1000,
				appconfig.bitrate_nom / 1000,
				appconfig.bitrate_max / 1000);
}

/* Calculate max peak and amplitude RMSD (based on all channels). */
static void peak_check_S16_LE(const int16_t *buffer, size_t frames, int channels,
		int16_t *peak, int16_t *rms) {

	const size_t size = frames * channels;
	int16_t abslvl;
	int64_t sum2;
	size_t x;

	*peak = 0;
	for (x = sum2 = 0; x < size; x++) {
		abslvl = abs(buffer[x]);
		if (*peak < abslvl)
			*peak = abslvl;
		sum2 += abslvl * abslvl;
	}
	*rms = sqrt(sum2 / frames);
}

/* Audio signal data reader thread. */
static void *reader_thread(void *arg) {
	(void)arg;

	int16_t *buffer = malloc(sizeof(int16_t) * appconfig.pcm_channels * READER_FRAMES);
	snd_pcm_sframes_t rd_len;
	int16_t signal_peak;
	int16_t signal_rms;

	struct timespec current_time;
	struct timespec peak_time = { 0 };

	while (main_loop_on) {
		rd_len = snd_pcm_readi(appconfig.pcm, buffer, READER_FRAMES);

		/* buffer overrun (this should not happen) */
		if (rd_len == -EPIPE) {
			snd_pcm_recover(appconfig.pcm, rd_len, 1);
			if (appconfig.verbose)
				warn("PCM buffer overrun");
			continue;
		}

		peak_check_S16_LE(buffer, rd_len, appconfig.pcm_channels, &signal_peak, &signal_rms);

		if (appconfig.signal_meter) {
			/* dump current peak and RMS values to the stdout */
			printf("\rsignal peak [%%]: %3u, signal RMS [%%]: %3u\r",
					signal_peak * 100 / 0x7ffe, signal_rms * 100 / 0x7ffe);
			fflush(stdout);
			continue;
		}

		/* if the max peak in the buffer is greater than the threshold, update
		 * the last peak time */
		if ((int)signal_peak * 100 / 0x7ffe > appconfig.threshold)
			clock_gettime(CLOCK_MONOTONIC_RAW, &peak_time);

		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
		if ((current_time.tv_sec - peak_time.tv_sec) * 1000 +
				(current_time.tv_nsec - peak_time.tv_nsec) / 1000000 < appconfig.fadeout_time) {

			pthread_mutex_lock(&appconfig.mutex);

			/* if this will happen, nothing is going to save us... */
			if (appconfig.current == appconfig.size) {
				appconfig.current = 0;
				if (appconfig.verbose)
					warn("Reader buffer overrun");
			}

			/* NOTE: The size of data returned by the pcm_read in the blocking mode is
			 *       always equal to the requested size. So, if the reader buffer (the
			 *       external one) is an integer multiplication of our internal buffer,
			 *       there is no need for any fancy boundary check. However, this might
			 *       not be true if someone is using CPU profiling tool, like cpulimit. */
			memcpy(&appconfig.buffer[appconfig.current], buffer, sizeof(int16_t) * rd_len * appconfig.pcm_channels);
			appconfig.current += rd_len * appconfig.pcm_channels;

			/* dump reader buffer usage */
			debug("Buffer usage: %d out of %d", appconfig.current, appconfig.size);

			pthread_cond_broadcast(&appconfig.ready);
			pthread_mutex_unlock(&appconfig.mutex);

		}
	}

	if (appconfig.signal_meter)
		printf("\n");

	/* avoid dead-lock on the condition wait during the exit */
	pthread_cond_broadcast(&appconfig.ready);

	free(buffer);
	return 0;
}

/* Audio signal data processing thread. */
static void *processing_thread(void *arg) {
	(void)arg;

	if (appconfig.signal_meter)
		return NULL;

	int16_t *buffer = malloc(sizeof(int16_t) * appconfig.pcm_channels * PROCESSING_FRAMES);
	size_t frames = 0;

	struct timespec current_time;
	struct timespec previous_time = { 0 };
	struct tm tmp_tm_time;
	time_t tmp_t_time;
	bool create_new_output = true;
	/* it must contain a prefix and the timestamp */
	char file_name[192];

	FILE *fp = NULL;

#if ENABLE_SNDFILE
	struct writer_sndfile *writer_sndfile = NULL;
	if (appconfig.output_format == FORMAT_WAV) {
		if ((writer_sndfile = writer_sndfile_init(appconfig.pcm_channels, appconfig.pcm_rate,
						SF_FORMAT_WAV | SF_FORMAT_PCM_16)) == NULL) {
			error("Couldn't initialize sndfile writer: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
#endif

#if ENABLE_MP3LAME
	struct writer_mp3lame *writer_mp3lame = NULL;
	if (appconfig.output_format == FORMAT_MP3) {
		if ((writer_mp3lame = writer_mp3lame_init(appconfig.pcm_channels, appconfig.pcm_rate,
						appconfig.bitrate_min, appconfig.bitrate_max, appconfig.banner)) == NULL) {
			error("Couldn't initialize mp3lame writer: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}
		if (appconfig.verbose >= 2)
			lame_print_internals(writer_mp3lame->gfp);
	}
#endif

#if ENABLE_VORBIS
	struct writer_vorbis *writer_vorbis = NULL;
	if (appconfig.output_format == FORMAT_OGG) {
		if ((writer_vorbis = writer_vorbis_init(appconfig.pcm_channels, appconfig.pcm_rate,
						appconfig.bitrate_min, appconfig.bitrate_nom, appconfig.bitrate_max,
						appconfig.banner)) == NULL) {
			error("Couldn't initialize vorbis writer: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
#endif

	while (main_loop_on) {

		/* copy data from the reader buffer into our internal one */
		pthread_mutex_lock(&appconfig.mutex);
		if (appconfig.current == 0) /* wait until new data are available */
			pthread_cond_wait(&appconfig.ready, &appconfig.mutex);
		memcpy(buffer, appconfig.buffer, sizeof(int16_t) * appconfig.current);
		frames = appconfig.current / appconfig.pcm_channels;
		appconfig.current = 0;
		pthread_mutex_unlock(&appconfig.mutex);

		/* check if new file should be created (activity time based) */
		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
		if (appconfig.split_time &&
				(current_time.tv_sec - previous_time.tv_sec) > appconfig.split_time)
			create_new_output = true;
		memcpy(&previous_time, &current_time, sizeof(previous_time));

		/* create new output file if needed */
		if (create_new_output) {
			create_new_output = false;

			tmp_t_time = time(NULL);
			localtime_r(&tmp_t_time, &tmp_tm_time);
			sprintf(file_name, "%s-%02d-%02d:%02d:%02d.%s",
					appconfig.output_prefix,
					tmp_tm_time.tm_mday, tmp_tm_time.tm_hour,
					tmp_tm_time.tm_min, tmp_tm_time.tm_sec,
					get_output_format_name(appconfig.output_format));

			if (appconfig.verbose)
				info("Creating new output file: %s", file_name);

			/* initialize new file for selected encoder */
			switch (appconfig.output_format) {
#if ENABLE_SNDFILE
			case FORMAT_WAV:
				if (writer_sndfile_open(writer_sndfile, file_name) != -1)
					break;
				error("Couldn't open sndfile writer: %s", strerror(errno));
				goto fail;
#endif
#if ENABLE_MP3LAME
			case FORMAT_MP3:
				if (writer_mp3lame_open(writer_mp3lame, file_name) != -1)
					break;
				error("Couldn't open mp3lame writer: %s", strerror(errno));
				goto fail;
#endif
#if ENABLE_VORBIS
			case FORMAT_OGG:
				if (writer_vorbis_open(writer_vorbis, file_name) != -1)
					break;
				error("Couldn't open vorbis writer: %s", strerror(errno));
				goto fail;
#endif
			case FORMAT_RAW:
				if (fp != NULL)
					fclose(fp);
				if ((fp = fopen(file_name, "w")) != NULL)
					break;
				error("Couldn't create output file: %s", strerror(errno));
				goto fail;
			}

		}

		/* use selected encoder for data processing */
		switch (appconfig.output_format) {
#if ENABLE_SNDFILE
		case FORMAT_WAV:
			writer_sndfile_write(writer_sndfile, buffer, frames);
			break;
#endif
#if ENABLE_MP3LAME
		case FORMAT_MP3:
			writer_mp3lame_write(writer_mp3lame, buffer, frames);
			break;
#endif
#if ENABLE_VORBIS
		case FORMAT_OGG:
			writer_vorbis_write(writer_vorbis, buffer, frames);
			break;
#endif
		case FORMAT_RAW:
			fwrite(buffer, sizeof(int16_t) * appconfig.pcm_channels, frames, fp);
		}

	}

fail:

	/* clean up routines for selected encoder */
	switch (appconfig.output_format) {
#if ENABLE_SNDFILE
	case FORMAT_WAV:
		writer_sndfile_free(writer_sndfile);
		break;
#endif
#if ENABLE_MP3LAME
	case FORMAT_MP3:
		writer_mp3lame_free(writer_mp3lame);
		break;
#endif
#if ENABLE_VORBIS
	case FORMAT_OGG:
		writer_vorbis_free(writer_vorbis);
		break;
#endif
	case FORMAT_RAW:
		if (fp != NULL)
			fclose(fp);
	}

	free(buffer);
	return 0;
}

int main(int argc, char *argv[]) {

	int opt;
	size_t i;
	const char *opts = "hvmD:R:C:l:f:o:p:s:";
	const struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"verbose", no_argument, NULL, 'v'},
		{"device", required_argument, NULL, 'D'},
		{"channels", required_argument, NULL, 'C'},
		{"rate", required_argument, NULL, 'R'},
		{"sig-level", required_argument, NULL, 'l'},
		{"fadeout-lag", required_argument, NULL, 'f'},
		{"out-format", required_argument, NULL, 'o'},
		{"out-prefix", required_argument, NULL, 'p'},
		{"split-time", required_argument, NULL, 's'},
		{"sig-meter", no_argument, NULL, 'm'},
		{0, 0, 0, 0},
	};

	/* print application banner */
	printf("%s\n", appconfig.banner);

	/* arguments parser */
	while ((opt = getopt_long(argc, argv, opts, longopts, NULL)) != -1)
		switch (opt) {
		case 'h' /* --help */ :
			printf("usage: %s [options]\n"
					"  -h, --help\t\t\tprint recipe for a delicious cake\n"
					"  -D DEV, --device=DEV\t\tselect audio input device (current: %s)\n"
					"  -R NN, --rate=NN\t\tset sample rate (current: %u)\n"
					"  -C NN, --channels=NN\t\tspecify number of channels (current: %u)\n"
					"  -l NN, --sig-level=NN\t\tactivation signal threshold (current: %u)\n"
					"  -f NN, --fadeout-lag=NN\tfadeout time lag in ms (current: %u)\n"
					"  -s NN, --split-time=NN\tsplit output file time in s (current: %d)\n"
					"  -p STR, --out-prefix=STR\toutput file prefix (current: %s)\n"
					"  -o FMT, --out-format=FMT\toutput file format (current: %s)\n"
					"  -m, --sig-meter\t\taudio signal level meter\n"
					"  -v, --verbose\t\t\tprint some extra information\n",
					argv[0],
					appconfig.pcm_device, appconfig.pcm_rate, appconfig.pcm_channels,
					appconfig.threshold, appconfig.fadeout_time,
					appconfig.split_time, appconfig.output_prefix,
					get_output_format_name(appconfig.output_format));
			return EXIT_SUCCESS;

		case 'm' /* --sig-meter */ :
			appconfig.signal_meter = true;
			break;
		case 'v' /* --verbose */ :
			appconfig.verbose++;
			break;

		case 'D' /* --device */ :
			strncpy(appconfig.pcm_device, optarg, sizeof(appconfig.pcm_device) - 1);
			break;
		case 'C' /* --channels */ :
			appconfig.pcm_channels = abs(atoi(optarg));
			break;
		case 'R' /* --rate */ :
			appconfig.pcm_rate = abs(atoi(optarg));
			break;

		case 'p' /* --out-prefix */ :
			strncpy(appconfig.output_prefix, optarg, sizeof(appconfig.output_prefix) - 1);
			break;
		case 'o' /* --out-format */ :
			for (i = 0; i < sizeof(output_formats) / sizeof(*output_formats); i++)
				if (strcasecmp(output_formats[i].name, optarg) == 0) {
					appconfig.output_format = output_formats[i].format;
					break;
				}
			if (i == sizeof(output_formats) / sizeof(*output_formats)) {
				fprintf(stderr, "error: Unknown output format [");
				for (i = 0; i < sizeof(output_formats) / sizeof(*output_formats); i++)
					fprintf(stderr, "%s%s", i != 0 ? ", " : "", output_formats[i].name);
				fprintf(stderr, "]: %s\n", optarg);
				return EXIT_FAILURE;
			}
			break;

		case 'l' /* --sig-level */ :
			appconfig.threshold = atoi(optarg);
			if (appconfig.threshold < 0 || appconfig.threshold > 100) {
				error("Signal level out of range [0, 100]: %d", appconfig.threshold);
				return EXIT_FAILURE;
			}
			break;
		case 'f' /* --fadeout-lag */ :
			appconfig.fadeout_time = atoi(optarg);
			if (appconfig.fadeout_time < 100 || appconfig.fadeout_time > 1000000) {
				error("Fadeout lag out of range [100, 1000000]: %d", appconfig.fadeout_time);
				return EXIT_FAILURE;
			}
			break;
		case 's' /* --split-time */ :
			appconfig.split_time = atoi(optarg);
			if (appconfig.split_time < 0 || appconfig.split_time > 1000000) {
				error("Split time out of range [0, 1000000]: %d", appconfig.split_time);
				return EXIT_FAILURE;
			}
			break;

		default:
			fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
			return EXIT_FAILURE;
		}

	int status = EXIT_SUCCESS;
	pthread_t thread_read_id;
	pthread_t thread_process_id;
	char *msg = NULL;
	int err;

	/* initialize reader data */
	pthread_mutex_init(&appconfig.mutex, NULL);
	pthread_cond_init(&appconfig.ready, NULL);
	appconfig.size = appconfig.pcm_channels * PROCESSING_FRAMES;
	appconfig.buffer = malloc(sizeof(int16_t) * appconfig.size);
	appconfig.current = 0;

	if (appconfig.buffer == NULL) {
		error("Failed to allocate memory for read buffer");
		goto fail;
	}

	if ((err = snd_pcm_open(&appconfig.pcm, appconfig.pcm_device, SND_PCM_STREAM_CAPTURE, 0)) != 0) {
		error("Couldn't open PCM device: %s", snd_strerror(err));
		goto fail;
	}

	if ((err = set_hw_params(appconfig.pcm, &msg)) != 0) {
		error("Couldn't set HW parameters: %s", msg);
		goto fail;
	}

	if ((err = snd_pcm_prepare(appconfig.pcm)) != 0) {
		error("Couldn't prepare PCM: %s", snd_strerror(err));
		goto fail;
	}

	if (appconfig.verbose)
		print_audio_info();

	struct sigaction sigact = { .sa_handler = main_loop_stop };
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);

	/* initialize thread for audio capturing */
	if (pthread_create(&thread_read_id, NULL, &reader_thread, NULL) != 0) {
		error("Couldn't create input thread");
		goto fail;
	}

	/* initialize thread for data processing */
	if (pthread_create(&thread_process_id, NULL, &processing_thread, NULL) != 0) {
		error("Couldn't create processing thread");
		goto fail;
	}

	pthread_join(thread_read_id, NULL);
	pthread_join(thread_process_id, NULL);

	status = EXIT_SUCCESS;
	goto success;

fail:
	status = EXIT_FAILURE;

success:
	if (appconfig.pcm != NULL)
		snd_pcm_close(appconfig.pcm);
	pthread_mutex_destroy(&appconfig.mutex);
	pthread_cond_destroy(&appconfig.ready);
	free(appconfig.buffer);
	return status;
}
