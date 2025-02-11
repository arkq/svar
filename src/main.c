/*
 * SVAR - main.c
 * SPDX-FileCopyrightText: 2010-2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
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

#if ENABLE_PORTAUDIO
# include <portaudio.h>
#else
# include <alsa/asoundlib.h>
#endif

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
#if ENABLE_SNDFILE
	FORMAT_WAV,
#endif
#if ENABLE_MP3LAME
	FORMAT_MP3,
#endif
#if ENABLE_VORBIS
	FORMAT_OGG,
#endif
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
	char pcm_device[25];
	int pcm_device_id;
	unsigned int pcm_channels;
	unsigned int pcm_rate;

	/* if true, run signal meter only */
	bool signal_meter;
	/* output verboseness level */
	int verbose;

	/* strftime() format for output file */
	const char *output;

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
	pthread_cond_t ready_cond;

	/* is the reader active */
	bool active;

	/* reader buffer */
	int16_t *buffer;
	size_t buffer_read_len;
	size_t buffer_size;

} appconfig = {

	.banner = "SVAR - Simple Voice Activated Recorder",

	.pcm_device = "default",
	.pcm_device_id = 0,
	.pcm_channels = 1,
	.pcm_rate = 44100,

	.signal_meter = false,
	.verbose = 0,

	/* strftime() format string for output file */
	.output = "rec-%d-%H:%M:%S",

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

	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.ready_cond = PTHREAD_COND_INITIALIZER,
	.active = true,

};

static void main_loop_stop(int sig) {
	appconfig.active = false;
	(void)sig;
}

#if !ENABLE_PORTAUDIO
/* Set ALSA hardware parameters. */
static int pcm_set_hw_params(snd_pcm_t *pcm, char **msg) {

	snd_pcm_hw_params_t *params;
	char buf[256];
	int dir = 0;
	int err;

	snd_pcm_hw_params_alloca(&params);

	if ((err = snd_pcm_hw_params_any(pcm, params)) < 0) {
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
#endif

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
			"Hardware parameters: %d Hz, S16LE, %d channel%s\n",
			appconfig.pcm_device,
			appconfig.pcm_rate,
			appconfig.pcm_channels, appconfig.pcm_channels > 1 ? "s" : "");
	if (!appconfig.signal_meter)
		printf("Output file format: %s\n",
				get_output_format_name(appconfig.output_format));
#if ENABLE_MP3LAME
	if (appconfig.output_format == FORMAT_MP3)
		printf("Output bit rate [min, max]: %d, %d kbit/s\n",
				appconfig.bitrate_min / 1000,
				appconfig.bitrate_max / 1000);
#endif
#if ENABLE_VORBIS
	if (appconfig.output_format == FORMAT_OGG)
		printf("Output bit rate [min, nominal, max]: %d, %d, %d kbit/s\n",
				appconfig.bitrate_min / 1000,
				appconfig.bitrate_nom / 1000,
				appconfig.bitrate_max / 1000);
#endif
}

#if ENABLE_PORTAUDIO
static void pa_list_devices(void) {

	PaDeviceIndex count = Pa_GetDeviceCount();
	unsigned int len = ceil(log10(count));
	const PaDeviceInfo *info;
	PaDeviceIndex i;

	printf(" %*s:\t%s\n", 1 + len, "ID", "Name");
	for (i = 0; i < count; i++) {
		if ((info = Pa_GetDeviceInfo(i))->maxInputChannels > 0)
			printf(" %c%*d:\t%s\n",
					i == appconfig.pcm_device_id ? '*' : ' ',
					len, i, info->name);
	}

}
#endif

/* Calculate max peak and amplitude RMS (based on all channels). */
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

	*rms = ceil(sqrt((double)sum2 / frames));
}

/* Process incoming audio frames. */
static void process_audio_S16_LE(const int16_t *buffer, size_t frames, int channels) {

	static struct timespec peak_time = { 0 };
	struct timespec current_time;

	int16_t signal_peak;
	int16_t signal_rms;

	peak_check_S16_LE(buffer, frames, channels, &signal_peak, &signal_rms);

	if (appconfig.signal_meter) {
		/* dump current peak and RMS values to the stdout */
		printf("\rsignal peak [%%]: %3u, signal RMS [%%]: %3u\r",
				signal_peak * 100 / 0x7ffe, signal_rms * 100 / 0x7ffe);
		fflush(stdout);
		return;
	}

	/* if the max peak in the buffer is greater than the threshold, update
	 * the last peak time */
	if ((int)signal_peak * 100 / 0x7ffe > appconfig.threshold)
		clock_gettime(CLOCK_MONOTONIC_RAW, &peak_time);

	clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
	if ((current_time.tv_sec - peak_time.tv_sec) * 1000 +
			(current_time.tv_nsec - peak_time.tv_nsec) / 1000000 < appconfig.fadeout_time) {

		pthread_mutex_lock(&appconfig.mutex);

		if (appconfig.buffer_read_len == appconfig.buffer_size) {
			/* Drop the current buffer, so we can process incoming data. */
			if (appconfig.verbose)
				warn("Reader buffer overrun");
			appconfig.buffer_read_len = 0;
		}

		/* NOTE: The size of data returned by the pcm_read in the blocking mode is
		 *       always equal to the requested size. So, if the reader buffer (the
		 *       external one) is an integer multiplication of our internal buffer,
		 *       there is no need for any fancy boundary check. However, this might
		 *       not be true if someone is using CPU profiling tool, like cpulimit. */
		memcpy(&appconfig.buffer[appconfig.buffer_read_len], buffer,
				sizeof(int16_t) * frames * appconfig.pcm_channels);
		appconfig.buffer_read_len += frames * appconfig.pcm_channels;

		/* dump reader buffer usage */
		debug("Buffer usage: %zd out of %zd", appconfig.buffer_read_len, appconfig.buffer_size);

		pthread_mutex_unlock(&appconfig.mutex);

		/* Notify the processing thread that new data are available. */
		pthread_cond_signal(&appconfig.ready_cond);

	}

}

#if ENABLE_PORTAUDIO

/* Callback function for PortAudio capture. */
static int pa_capture_callback(const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags, void *userData) {
	(void)outputBuffer;
	(void)timeInfo;
	(void)statusFlags;
	(void)userData;
	process_audio_S16_LE(inputBuffer, framesPerBuffer, appconfig.pcm_channels);
	return appconfig.active ? paContinue : paComplete;
}

#else

/* Thread function for ALSA capture. */
static void *alsa_capture_thread(void *arg) {

	int16_t *buffer;
	if ((buffer = malloc(sizeof(int16_t) * appconfig.pcm_channels * READER_FRAMES)) == NULL) {
		error("Couldn't allocate memory for capturing PCM: %s", strerror(errno));
		return NULL;
	}

	snd_pcm_sframes_t frames;
	snd_pcm_t *pcm = arg;

	while (appconfig.active) {
		if ((frames = snd_pcm_readi(pcm, buffer, READER_FRAMES)) < 0)
			switch (frames) {
			case -EPIPE:
			case -ESTRPIPE:
				snd_pcm_recover(pcm, frames, 1);
				if (appconfig.verbose)
					warn("PCM buffer overrun: %s", snd_strerror(frames));
				continue;
			case -ENODEV:
				error("PCM read error: %s", "Device disconnected");
				goto fail;
			default:
				error("PCM read error: %s", snd_strerror(frames));
				continue;
			}
		process_audio_S16_LE(buffer, frames, appconfig.pcm_channels);
	}

fail:
	free(buffer);
	return NULL;
}

#endif

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
	char file_name_tmp[192];
	char file_name[192 + 4];

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

	while (appconfig.active) {

		/* copy data from the reader buffer into our internal one */
		pthread_mutex_lock(&appconfig.mutex);
		while (appconfig.active && appconfig.buffer_read_len == 0)
			pthread_cond_wait(&appconfig.ready_cond, &appconfig.mutex);
		memcpy(buffer, appconfig.buffer, sizeof(int16_t) * appconfig.buffer_read_len);
		frames = appconfig.buffer_read_len / appconfig.pcm_channels;
		appconfig.buffer_read_len = 0;
		pthread_mutex_unlock(&appconfig.mutex);

		if (frames == 0)
			continue;

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

			strftime(file_name_tmp, sizeof(file_name_tmp), appconfig.output, &tmp_tm_time);
			snprintf(file_name, sizeof(file_name), "%s.%s",
					file_name_tmp, get_output_format_name(appconfig.output_format));

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
	const char *opts = "hVvLD:R:C:l:f:o:s:m";
	const struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{"verbose", no_argument, NULL, 'v'},
		{"list-devices", no_argument, NULL, 'L'},
		{"device", required_argument, NULL, 'D'},
		{"channels", required_argument, NULL, 'C'},
		{"rate", required_argument, NULL, 'R'},
		{"sig-level", required_argument, NULL, 'l'},
		{"fadeout-lag", required_argument, NULL, 'f'},
		{"out-format", required_argument, NULL, 'o'},
		{"split-time", required_argument, NULL, 's'},
		{"sig-meter", no_argument, NULL, 'm'},
		{0, 0, 0, 0},
	};

#if ENABLE_PORTAUDIO
	PaError pa_err;
	if ((pa_err = Pa_Initialize()) != paNoError) {
		error("Couldn't initialize PortAudio: %s", Pa_GetErrorText(pa_err));
		return EXIT_FAILURE;
	}
	if ((appconfig.pcm_device_id = Pa_GetDefaultInputDevice()) == paNoDevice)
		warn("Couldn't get default input PortAudio device");
#endif

	/* arguments parser */
	while ((opt = getopt_long(argc, argv, opts, longopts, NULL)) != -1)
		switch (opt) {
		case 'h' /* --help */ :
			printf("Usage:\n"
					"  %s [options] [output-template]\n"
					"\nOptions:\n"
					"  -h, --help\t\t\tprint recipe for a delicious cake\n"
					"  -V, --version\t\t\tprint version number and exit\n"
					"  -v, --verbose\t\t\tprint some extra information\n"
#if ENABLE_PORTAUDIO
					"  -L, --list-devices\t\tlist available audio input devices\n"
					"  -D ID, --device=ID\t\tselect audio input device (current: %d)\n"
#else
					"  -D DEV, --device=DEV\t\tselect audio input device (current: %s)\n"
#endif
					"  -R NN, --rate=NN\t\tset sample rate (current: %u)\n"
					"  -C NN, --channels=NN\t\tspecify number of channels (current: %u)\n"
					"  -l NN, --sig-level=NN\t\tactivation signal threshold (current: %u)\n"
					"  -f NN, --fadeout-lag=NN\tfadeout time lag in ms (current: %u)\n"
					"  -s NN, --split-time=NN\tsplit output file time in s (current: %d)\n"
					"  -o FMT, --out-format=FMT\toutput file format (current: %s)\n"
					"  -m, --sig-meter\t\taudio signal level meter\n"
					"\n"
					"The output-template argument is a strftime(3) format string which\n"
					"will be used for creating output file name. If not specified, the\n"
					"default value is: %s + extension\n",
					argv[0],
#if ENABLE_PORTAUDIO
					appconfig.pcm_device_id,
#else
					appconfig.pcm_device,
#endif
					appconfig.pcm_rate,
					appconfig.pcm_channels,
					appconfig.threshold,
					appconfig.fadeout_time,
					appconfig.split_time,
					get_output_format_name(appconfig.output_format),
					appconfig.output);
			return EXIT_SUCCESS;

		case 'V' /* --version */ :
			printf("%s\n", PROJECT_VERSION);
			return EXIT_SUCCESS;

		case 'm' /* --sig-meter */ :
			appconfig.signal_meter = true;
			break;
		case 'v' /* --verbose */ :
			appconfig.verbose++;
			break;

#if ENABLE_PORTAUDIO
		case 'L' /* --list-devices */ :
			pa_list_devices();
			return EXIT_SUCCESS;
		case 'D' /* --device=ID */ :
			appconfig.pcm_device_id = atoi(optarg);
			break;
#else
		case 'D' /* --device=DEV */ :
			strncpy(appconfig.pcm_device, optarg, sizeof(appconfig.pcm_device) - 1);
			break;
#endif

		case 'C' /* --channels */ :
			appconfig.pcm_channels = abs(atoi(optarg));
			break;
		case 'R' /* --rate */ :
			appconfig.pcm_rate = abs(atoi(optarg));
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

	if (optind < argc)
		appconfig.output = argv[optind];

	/* print application banner */
	printf("%s\n", appconfig.banner);

#if !ENABLE_PORTAUDIO
	pthread_t thread_alsa_capture_id;
#endif
	pthread_t thread_process_id;
	int err;

	/* initialize reader data */
	appconfig.buffer_size = appconfig.pcm_channels * PROCESSING_FRAMES;
	appconfig.buffer = malloc(sizeof(int16_t) * appconfig.buffer_size);
	appconfig.buffer_read_len = 0;

	if (appconfig.buffer == NULL) {
		error("Failed to allocate memory for read buffer");
		return EXIT_FAILURE;
	}

#if ENABLE_PORTAUDIO

	PaStream *pa_stream = NULL;
	PaStreamParameters pa_params = {
		.sampleFormat = paInt16,
		.device = appconfig.pcm_device_id,
		.channelCount = appconfig.pcm_channels,
		.suggestedLatency = Pa_GetDeviceInfo(appconfig.pcm_device_id)->defaultLowInputLatency,
		.hostApiSpecificStreamInfo = NULL,
	};

	if ((pa_err = Pa_OpenStream(&pa_stream, &pa_params, NULL, appconfig.pcm_rate,
					READER_FRAMES, paClipOff, pa_capture_callback, NULL)) != paNoError) {
		error("Couldn't open PortAudio stream: %s", Pa_GetErrorText(pa_err));
		return EXIT_FAILURE;
	}

#else

	snd_pcm_t *pcm;
	char *msg;

	if ((err = snd_pcm_open(&pcm, appconfig.pcm_device, SND_PCM_STREAM_CAPTURE, 0)) != 0) {
		error("Couldn't open PCM device: %s", snd_strerror(err));
		return EXIT_FAILURE;
	}

	if ((err = pcm_set_hw_params(pcm, &msg)) != 0) {
		error("Couldn't set HW parameters: %s", msg);
		return EXIT_FAILURE;
	}

	if ((err = snd_pcm_prepare(pcm)) != 0) {
		error("Couldn't prepare PCM: %s", snd_strerror(err));
		return EXIT_FAILURE;
	}

#endif

	if (appconfig.verbose)
		print_audio_info();

	struct sigaction sigact = { .sa_handler = main_loop_stop, .sa_flags = SA_RESETHAND };
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);

#if ENABLE_PORTAUDIO
	if ((pa_err = Pa_StartStream(pa_stream)) != paNoError) {
		error("Couldn't start PortAudio stream: %s", Pa_GetErrorText(pa_err));
		return EXIT_FAILURE;
	}
#else
	if ((err = pthread_create(&thread_alsa_capture_id, NULL, &alsa_capture_thread, pcm)) != 0) {
		error("Couldn't create ALSA capture thread: %s", strerror(-err));
		return EXIT_FAILURE;
	}
#endif

	/* initialize thread for data processing */
	if ((err = pthread_create(&thread_process_id, NULL, &processing_thread, NULL)) != 0) {
		error("Couldn't create processing thread: %s", strerror(-err));
		return EXIT_FAILURE;
	}

#if ENABLE_PORTAUDIO
	while ((pa_err = Pa_IsStreamActive(pa_stream)) == 1)
		Pa_Sleep(1000);
	if (pa_err < 0)
		error("Couldn't check PortAudio activity: %s", Pa_GetErrorText(pa_err));
#else
	pthread_join(thread_alsa_capture_id, NULL);
#endif

	/* Gracefully stop the processing thread. */
	pthread_mutex_lock(&appconfig.mutex);
	appconfig.active = false;
	pthread_mutex_unlock(&appconfig.mutex);
	pthread_cond_signal(&appconfig.ready_cond);

	pthread_join(thread_process_id, NULL);

	if (appconfig.signal_meter)
		printf("\n");

	return EXIT_SUCCESS;
}
