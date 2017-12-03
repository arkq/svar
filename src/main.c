/*
 * SVAR - main.c
 * Copyright (c) 2010-2017 Arkadiusz Bokowy
 *
 * This file is a part of SVAR.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <alsa/asoundlib.h>
#if ENABLE_SNDFILE
# include <sndfile.h>
#endif
#if ENABLE_VORBIS
# include <vorbis/vorbisenc.h>
#endif

#include "debug.h"


enum encoding_format {
	ENC_FORMAT_RAW = 0,
	ENC_FORMAT_WAVE,
	ENC_FORMAT_VORBIS,
};

struct encoding_format_info_t {
	enum encoding_format id;
	char name[16];
};

struct appconfig_t {
	char output_prefix[128];

	/* if true, run signal meter only */
	bool signal_meter;
	/* if true, print debug messages */
	bool verbose;

	int threshold;    /* % of max signal */
	int fadeout_time; /* in ms */
	int split_time;   /* in s (0 disables split) */

	enum encoding_format encoder;

	/* variable bit rate settings for encoder (bit per second) */
	int bitrate_min;
	int bitrate_nom;
	int bitrate_max;
};

struct hwconfig_t {
	char device[25];
	snd_pcm_format_t format;
	snd_pcm_access_t access;
	unsigned int rate, channels;
};

#define READER_FRAMES 512 * 8
#define PROCESSING_FRAMES READER_FRAMES * 16

struct hwreader_t {
	struct hwconfig_t *hw;
	snd_pcm_t *handle;

	pthread_mutex_t mutex;
	pthread_cond_t ready;
	int16_t *buffer;
	/* current position */
	int current;
	/* buffer size */
	int size;
};


/* global application settings */
static struct appconfig_t appconfig;
static struct encoding_format_info_t appencoders[] = {
#if ENABLE_VORBIS
	{ ENC_FORMAT_VORBIS, "ogg" },
#endif
#if ENABLE_SNDFILE
	{ ENC_FORMAT_WAVE, "wav" },
#endif
	{ ENC_FORMAT_RAW, "raw" },
};


/* Set hardware parameters. */
static int set_hwparams(snd_pcm_t *handle, struct hwconfig_t *hwconf) {

	snd_pcm_hw_params_t *hw_params;
	int rv;

	if ((rv = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
		fprintf(stderr, "error: cannot allocate hw_param struct\n");
		return rv;
	}

	/* choose all parameters */
	if ((rv = snd_pcm_hw_params_any(handle, hw_params)) < 0) {
		fprintf(stderr, "error: broken configuration (%s)\n", snd_strerror(rv));
		return rv;
	}

	/* set hardware access */
	if ((rv = snd_pcm_hw_params_set_access(handle, hw_params, hwconf->access)) < 0) {
		fprintf(stderr, "error: cannot set access type (%s)\n", snd_strerror(rv));
		return rv;
	}

	/* set sample format */
	if ((rv = snd_pcm_hw_params_set_format(handle, hw_params, hwconf->format)) < 0) {
		fprintf(stderr, "error: cannot set sample format (%s)\n", snd_strerror(rv));
		return rv;
	}

	/* set the count of channels */
	if ((rv = snd_pcm_hw_params_set_channels_near(handle, hw_params, &hwconf->channels)) < 0) {
		fprintf(stderr, "error: cannot set channel count (%s)\n", snd_strerror(rv));
		return rv;
	}

	/* set the stream rate */
	if ((rv = snd_pcm_hw_params_set_rate_near(handle, hw_params, &hwconf->rate, 0)) < 0) {
		fprintf(stderr, "error: cannot set sample rate (%s)\n", snd_strerror(rv));
		return rv;
	}

	/* write the parameters to device */
	if ((rv = snd_pcm_hw_params(handle, hw_params)) < 0) {
		fprintf(stderr, "error: unable to set hw params (%s)\n", snd_strerror(rv));
		return rv;
	}

	snd_pcm_hw_params_free(hw_params);
	return 0;
}

/* Return the name of a given output format. */
static const char *get_encoder_name(enum encoding_format format) {
	size_t i;
	for (i = 0; i < sizeof(appencoders) / sizeof(struct encoding_format_info_t); i++)
		if (appencoders[i].id == format)
			return appencoders[i].name;
	return NULL;
}

/* Print some information about the audio device and its configuration. */
static void print_audio_info(struct hwconfig_t *hwconf) {
	printf("Capturing audio device: %s\n"
			"Hardware parameters: %iHz, %s, %i channel%s\n"
			"Output file format: %s\n",
			hwconf->device, hwconf->rate,
			snd_pcm_format_name(hwconf->format),
			hwconf->channels, hwconf->channels > 1 ? "s" : "",
			get_encoder_name(appconfig.encoder));
	if (appconfig.encoder == ENC_FORMAT_VORBIS)
		printf("  bitrates: %d, %d, %d kbit/s\n",
				appconfig.bitrate_min / 1000,
				appconfig.bitrate_nom / 1000,
				appconfig.bitrate_max / 1000);
}

/* Setup Ctrl-C signal catch for application quit. */
static int looop_mode;
static void stop_loop_mode(int sig){
	(void)sig;
	looop_mode = 0;
}

static void setup_quit_sigaction() {
	struct sigaction sigact = { .sa_handler = stop_loop_mode };
	if (sigaction(SIGINT, &sigact, NULL) == -1)
		perror("warning: Setting signal handler failed");
}

/* Calculate max peak and amplitude RMSD (based on all channels). */
static void peak_check_S16_LE(const int16_t *buffer, int frames, int channels,
		int16_t *peak, int16_t *rms) {
	const int size = frames * channels;
	int16_t abslvl;
	int64_t sum2;
	int x;

	*peak = 0;
	for (x = sum2 = 0; x < size; x++) {
		abslvl = abs(((int16_t *)buffer)[x]);
		if (*peak < abslvl)
			*peak = abslvl;
		sum2 += abslvl * abslvl;
	}
	*rms = sqrt(sum2 / frames);
}

#if ENABLE_VORBIS
/* Compress and write stream to the OGG file. */
static int do_analysis_and_write_ogg(vorbis_dsp_state *vd, vorbis_block *vb,
		ogg_stream_state *os, FILE *fp) {

	ogg_packet o_pack;
	ogg_page o_page;
	int wr_len = 0;

	/* do main analysis and create packets */
	while (vorbis_analysis_blockout(vd, vb) == 1) {
		vorbis_analysis(vb, NULL);
		vorbis_bitrate_addblock(vb);

		while (vorbis_bitrate_flushpacket(vd, &o_pack)) {
			ogg_stream_packetin(os, &o_pack);

			/* form OGG pages and write it to output file */
			while (ogg_stream_pageout(os, &o_page)) {
				wr_len += fwrite(o_page.header, 1, o_page.header_len, fp);
				wr_len += fwrite(o_page.body, 1, o_page.body_len, fp);
			}
		}
	}

	return wr_len;
}
#endif

/* Audio signal data reader thread. */
static void *reader_thread(void *ptr) {
	struct hwreader_t *data = (struct hwreader_t *)ptr;
	int16_t *buffer = (int16_t *)malloc(sizeof(int16_t) * data->hw->channels * READER_FRAMES);
	int16_t signal_peak;
	int16_t signal_rmsd;
	int rd_len;

	struct timespec current_time;
	struct timespec peak_time = { 0 };

	while (looop_mode) {
		rd_len = snd_pcm_readi(data->handle, buffer, READER_FRAMES);

		if (rd_len == -EPIPE) { /* buffer overrun (this should not happen) */
			snd_pcm_recover(data->handle, rd_len, 1);
			if (appconfig.verbose) {
				printf("warning: pcm_readi buffer overrun\n");
				fflush(stdout);
			}
			continue;
		}

		peak_check_S16_LE(buffer, rd_len, data->hw->channels, &signal_peak, &signal_rmsd);

		if (appconfig.signal_meter) {
			/* dump current peak and RMS values to the stdout */
			printf("\rsignal peak [%%]: %3u, siganl RMS [%%]: %3u\r",
					signal_peak * 100 / 0x7ffe, signal_rmsd * 100 / 0x7ffe);
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

			pthread_mutex_lock(&data->mutex);

			/* if this will happen, nothing is going to save us... */
			if (data->current == data->size) {
				data->current = 0;
				if (appconfig.verbose) {
					printf("warning: reader buffer overrun\n");
					fflush(stdout);
				}
			}

			/* NOTE: The size of data returned by the pcm_read in the blocking mode is
			 *       always equal to the requested size. So, if the reader buffer (the
			 *       external one) is an integer multiplication of our internal buffer,
			 *       there is no need for any fancy boundary check. However, this might
			 *       not be true if someone is using CPU profiling tool, like cpulimit. */
			memcpy(&data->buffer[data->current], buffer, sizeof(int16_t) * rd_len * data->hw->channels);
			data->current += rd_len * data->hw->channels;

			/* dump reader buffer usage */
			debug("buffer usage: %d of %d", data->current, data->size);

			pthread_cond_broadcast(&data->ready);
			pthread_mutex_unlock(&data->mutex);

		}
	}

	if (appconfig.signal_meter)
		printf("\n");

	/* avoid dead-lock on the condition wait during the exit */
	pthread_cond_broadcast(&data->ready);

	free(buffer);
	return 0;
}

/* Audio signal data processing thread. */
static void *processing_thread(void *ptr) {
	struct hwreader_t *data = (struct hwreader_t *)ptr;
	const int channels = data->hw->channels;
	int16_t *buffer = (int16_t *)malloc(sizeof(int16_t) * channels * PROCESSING_FRAMES);
	int frames;

	struct timespec current_time;
	struct timespec previous_time = { 0 };
	struct tm tmp_tm_time;
	time_t tmp_t_time;
	int create_new_output = 1;
	/* it must contain a prefix and the timestamp */
	char file_name[192];

	FILE *fp = NULL;

#if ENABLE_SNDFILE
	SNDFILE *sffp = NULL;
	SF_INFO sfinfo = {
		.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16,
		.samplerate = data->hw->rate,
		.channels = channels,
	};
#endif /* ENABLE_SNDFILE */

#if ENABLE_VORBIS
	ogg_stream_state ogg_s;
	ogg_packet ogg_p_main;
	ogg_packet ogg_p_comm;
	ogg_packet ogg_p_code;
	vorbis_info vbs_i;
	vorbis_dsp_state vbs_d;
	vorbis_block vbs_b;
	vorbis_comment vbs_c;
	float **vorbis_buffer;
	int fi, ci;

	if (appconfig.encoder == ENC_FORMAT_VORBIS) {
		vorbis_info_init(&vbs_i);
		vorbis_comment_init(&vbs_c);
		if (vorbis_encode_init(&vbs_i, channels, data->hw->rate,
				appconfig.bitrate_max, appconfig.bitrate_nom, appconfig.bitrate_min) < 0) {
			fprintf(stderr, "error: invalid parameters for vorbis bit rate\n");
			goto fail;
		}
		vorbis_comment_add(&vbs_c, "SVAR - Simple Voice Activated Recorder");
	}
#endif /* ENABLE_VORBIS */

	if (appconfig.signal_meter)
		/* it is not a failure, though, we just want to politely skip the loop */
		goto fail;

	while (looop_mode) {

		/* copy data from the reader buffer into our internal one */
		pthread_mutex_lock(&data->mutex);
		if (data->current == 0) /* wait until new data are available */
			pthread_cond_wait(&data->ready, &data->mutex);
		memcpy(buffer, data->buffer, sizeof(int16_t) * data->current);
		frames = data->current / channels;
		data->current = 0;
		pthread_mutex_unlock(&data->mutex);

		/* check if new file should be created (activity time based) */
		clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
		if (appconfig.split_time &&
				(current_time.tv_sec - previous_time.tv_sec) > appconfig.split_time)
			create_new_output = 1;
		memcpy(&previous_time, &current_time, sizeof(previous_time));

		/* create new output file if needed */
		if (create_new_output) {
			create_new_output = 0;

			tmp_t_time = time(NULL);
			localtime_r(&tmp_t_time, &tmp_tm_time);
			sprintf(file_name, "%s-%02d-%02d:%02d:%02d.%s",
					appconfig.output_prefix,
					tmp_tm_time.tm_mday, tmp_tm_time.tm_hour,
					tmp_tm_time.tm_min, tmp_tm_time.tm_sec,
					get_encoder_name(appconfig.encoder));

			if (appconfig.verbose)
				printf("info: creating new output file: %s\n", file_name);

			/* initialize new file for selected encoder */
			switch (appconfig.encoder) {
#if ENABLE_SNDFILE
			case ENC_FORMAT_WAVE:
				if (sffp)
					sf_close(sffp);
				if ((sffp = sf_open(file_name, SFM_WRITE, &sfinfo)) == NULL) {
					fprintf(stderr, "error: unable to create output file\n");
					goto fail;
				}
				break;

#endif /* ENABLE_SNDFILE */
#if ENABLE_VORBIS
			case ENC_FORMAT_VORBIS:

				if (fp) { /* close previously initialized file */

					/* indicate end of data */
					vorbis_analysis_wrote(&vbs_d, 0);
					do_analysis_and_write_ogg(&vbs_d, &vbs_b, &ogg_s, fp);

					ogg_stream_clear(&ogg_s);
					vorbis_block_clear(&vbs_b);
					vorbis_dsp_clear(&vbs_d);
					fclose(fp);
				}

				if ((fp = fopen(file_name, "w")) == NULL) {
					fprintf(stderr, "error: unable to create output file\n");
					goto fail;
				}

				/* initialize varbis analyzer every new OGG file */
				vorbis_analysis_init(&vbs_d, &vbs_i);
				vorbis_block_init(&vbs_d, &vbs_b);
				ogg_stream_init(&ogg_s, current_time.tv_sec);

				/* write three header packets to the OGG stream */
				vorbis_analysis_headerout(&vbs_d, &vbs_c, &ogg_p_main, &ogg_p_comm, &ogg_p_code);
				ogg_stream_packetin(&ogg_s, &ogg_p_main);
				ogg_stream_packetin(&ogg_s, &ogg_p_comm);
				ogg_stream_packetin(&ogg_s, &ogg_p_code);
				break;

#endif /* ENABLE_VORBIS */
			case ENC_FORMAT_RAW:
			default:
				if (fp)
					fclose(fp);
				if ((fp = fopen(file_name, "w")) == NULL) {
					fprintf(stderr, "error: unable to create output file\n");
					goto fail;
				}
			}
		}

		/* use selected encoder for data processing */
		switch (appconfig.encoder) {
#if ENABLE_SNDFILE
		case ENC_FORMAT_WAVE:
			sf_writef_short(sffp, buffer, frames);
			break;
#endif /* ENABLE_SNDFILE */
#if ENABLE_VORBIS
		case ENC_FORMAT_VORBIS:
			vorbis_buffer = vorbis_analysis_buffer(&vbs_d, frames);
			/* convert ALSA buffer into the vorbis one */
			for (fi = 0; fi < frames; fi++)
				for (ci = 0; ci < channels; ci++)
					vorbis_buffer[ci][fi] = (float)(buffer[fi * channels + ci]) / 0x7ffe;
			vorbis_analysis_wrote(&vbs_d, frames);
			do_analysis_and_write_ogg(&vbs_d, &vbs_b, &ogg_s, fp);
			break;
#endif /* ENABLE_VORBIS */
		case ENC_FORMAT_RAW:
		default:
			fwrite(buffer, sizeof(int16_t) * channels, frames, fp);
		}

	}

fail:

	/* clean up routines for selected encoder */
	switch (appconfig.encoder) {
#if ENABLE_SNDFILE
	case ENC_FORMAT_WAVE:
		if (sffp)
			sf_close(sffp);
		break;
#endif /* ENABLE_SNDFILE */
#if ENABLE_VORBIS
	case ENC_FORMAT_VORBIS:
		if (fp) {
			/* indicate end of data */
			vorbis_analysis_wrote(&vbs_d, 0);
			do_analysis_and_write_ogg(&vbs_d, &vbs_b, &ogg_s, fp);

			ogg_stream_clear(&ogg_s);
			vorbis_block_clear(&vbs_b);
			vorbis_dsp_clear(&vbs_d);
			fclose(fp);
		}
		vorbis_comment_clear(&vbs_c);
		vorbis_info_clear(&vbs_i);
		break;
#endif /* ENABLE_VORBIS */
	case ENC_FORMAT_RAW:
	default:
		if (fp)
			fclose(fp);
	}

	free(buffer);
	return 0;
}

int main(int argc, char *argv[]) {

	int opt;
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
	pthread_t thread_read_id;
	pthread_t thread_process_id;
	struct hwconfig_t hwconf;
	struct hwreader_t hwreader;
	int status;
	int rv, i;

	strcpy(appconfig.output_prefix, "rec");
	appconfig.threshold = 2;
	appconfig.fadeout_time = 500;
	appconfig.split_time = 0;
	appconfig.signal_meter = 0;

	/* default audio encoder */
#if ENABLE_SNDFILE
	appconfig.encoder = ENC_FORMAT_WAVE;
#elif ENABLE_VORBIS
	appconfig.encoder = ENC_FORMAT_VORBIS;
#else
	appconfig.encoder = ENC_FORMAT_RAW;
#endif

	/* default compression settings */
	appconfig.bitrate_min = 32000;
	appconfig.bitrate_nom = 64000;
	appconfig.bitrate_max = 128000;

	/* default input audio device settings */
	strcpy(hwconf.device, "hw:0,0");
	hwconf.format = SND_PCM_FORMAT_S16_LE;         /* modifying this is not recommended */
	hwconf.access = SND_PCM_ACCESS_RW_INTERLEAVED; /* whole audio code is based on it... */
	hwconf.rate = 44100;
	hwconf.channels = 1;

	/* print application banner, just for the lulz */
	printf("SVAR - Simple Voice Activated Recorder\n");

	/* arguments parser */
	while ((opt = getopt_long(argc, argv, opts, longopts, NULL)) != -1)
		switch (opt) {
		case 'h' /* --help */ :
			printf("usage: svar [options]\n"
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
					hwconf.device, hwconf.rate, hwconf.channels,
					appconfig.threshold, appconfig.fadeout_time,
					appconfig.split_time, appconfig.output_prefix,
					get_encoder_name(appconfig.encoder));
			return EXIT_SUCCESS;

		case 'm' /* --sig-meter */ :
			appconfig.signal_meter = true;
			break;
		case 'v' /* --verbose */ :
			appconfig.verbose = true;
			break;

		case 'D' /* --device */ :
			strncpy(hwconf.device, optarg, sizeof(hwconf.device) - 1);
			hwconf.device[sizeof(hwconf.device) - 1] = '\0';
			break;
		case 'C' /* --channels */ :
			hwconf.channels = atoi(optarg);
			break;
		case 'R' /* --rate */ :
			hwconf.rate = atoi(optarg);
			break;

		case 'p' /* --out-prefix */ :
			strncpy(appconfig.output_prefix, optarg, sizeof(appconfig.output_prefix) - 1);
			break;
		case 'o' /* --out-format */ :
			for (i = 0; (size_t)i < sizeof(appencoders) / sizeof(struct encoding_format_info_t); i++)
				if (strcasecmp(appencoders[i].name, optarg) == 0)
					appconfig.encoder = appencoders[i].id;
			if (i == sizeof(appencoders) / sizeof(struct encoding_format_info_t))
				printf("warning: format not available, leaving default\n");
			break;

		case 'l' /* --sig-level */ :
			rv = atoi(optarg);
			if (rv < 0 || rv > 100)
				printf("warning: sig-level out of range [0, 100] (%d),"
						" leaving default\n", rv);
			else
				appconfig.threshold = rv;
			break;
		case 'f' /* --fadeout-lag */ :
			rv = atoi(optarg);
			if (rv < 100 || rv > 1000000)
				printf("warning: fadeout-lag out of range [100, 1000000] (%d),"
						" leaving default\n", rv);
			else
				appconfig.fadeout_time = rv;
			break;
		case 's' /* --split-time */ :
			rv = atoi(optarg);
			if (rv < 0 || rv > 1000000)
				printf("warning: split-time out of range [0, 1000000] (%d),"
						" leaving default\n", rv);
			else
				appconfig.split_time = rv;
			break;

		default:
			fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
			return EXIT_FAILURE;
		}

	/* initialize reader structure */
	hwreader.hw = &hwconf;
	hwreader.handle = NULL;
	pthread_mutex_init(&hwreader.mutex, NULL);
	pthread_cond_init(&hwreader.ready, NULL);
	hwreader.size = hwconf.channels * PROCESSING_FRAMES;
	hwreader.buffer = (int16_t *)malloc(sizeof(int16_t) * hwreader.size);
	hwreader.current = 0;

	if (hwreader.buffer == NULL) {
		fprintf(stderr, "error: failed to allocate memory for read buffer\n");
		goto fail;
	}

	/* open audio device */
	if ((rv = snd_pcm_open(&hwreader.handle, hwconf.device, SND_PCM_STREAM_CAPTURE, 0)) != 0) {
		fprintf(stderr, "error: couldn't open PCM device: %s\n", snd_strerror(rv));
		goto fail;
	}

	/* set hardware parameters */
	if (set_hwparams(hwreader.handle, &hwconf) < 0)
		goto fail;

	if ((rv = snd_pcm_prepare(hwreader.handle)) != 0) {
		fprintf(stderr, "error: couldn't prepare PCM: %s\n", snd_strerror(rv));
		goto fail;
	}

	if (appconfig.verbose)
		print_audio_info(&hwconf);

	looop_mode = 1;
	setup_quit_sigaction();

	/* initialize thread for audio capturing */
	if (pthread_create(&thread_read_id, NULL, &reader_thread, &hwreader) != 0) {
		fprintf(stderr, "error: couldn't create input thread\n");
		goto fail;
	}
	/* initialize thread for data processing */
	if (pthread_create(&thread_process_id, NULL, &processing_thread, &hwreader) != 0) {
		fprintf(stderr, "error: couldn't create processing thread\n");
		goto fail;
	}

	pthread_join(thread_read_id, NULL);
	pthread_join(thread_process_id, NULL);

	status = EXIT_SUCCESS;
	goto success;

fail:
	status = EXIT_FAILURE;

success:
	snd_pcm_close(hwreader.handle);
	pthread_mutex_destroy(&hwreader.mutex);
	pthread_cond_destroy(&hwreader.ready);
	free(hwreader.buffer);
	return status;
}
