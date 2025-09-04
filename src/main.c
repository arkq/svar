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
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "log.h"
#include "pcm.h"
#include "recorder.h"
#include "writer.h"

#if ENABLE_ALSA
# include "recorder-alsa.h"
#endif
#if ENABLE_PIPEWIRE
# include "recorder-pipewire.h"
#endif
#if ENABLE_PORTAUDIO
# include "recorder-portaudio.h"
#endif

#if ENABLE_MP3LAME
# include "writer-mp3.h"
#endif
#if ENABLE_SNDFILE
# include "writer-wav.h"
#endif
#if ENABLE_VORBIS
# include "writer-ogg.h"
#endif

/* Application banner used for output file comment string. */
static const char * banner = "SVAR - Simple Voice Activated Recorder";
/* The verbose level used for debugging. */
static int verbose = 0;

/* Selected capturing PCM device. */
static const char * pcm_device = "default";
static enum pcm_format pcm_format = PCM_FORMAT_S16LE;
static unsigned int pcm_channels = 1;
static unsigned int pcm_rate = 44100;

/* If true, list available audio input devices and exit. */
static bool list_devices = false;
/* If true, run signal meter only. */
static bool signal_meter = false;

/* Selected output writer. */
static struct writer * writer = NULL;
/* Selected audio recorder. */
static struct recorder * recorder = NULL;

/* The strftime() format template for output file. */
static const char * template = "rec-%d-%H:%M:%S";

/* Variable bit rate settings for writers which support it. */
static int bitrate_min = 32000;
static int bitrate_nom = 64000;
static int bitrate_max = 128000;

/* The activation threshold level in dB. */
static double activation_threshold_level_db = -50.0;
/* The activation fadeout time - the time after the last activation signal. */
static long activation_fadeout_time_ms = 500;
/* The split time in seconds for creating new output file. */
static long output_split_time_ms = 0; /* disable splitting by default */

static void main_loop_stop(int sig) {
	recorder_stop(recorder);
	(void)sig;
}

/* Print some information about the audio device and its configuration. */
static void print_audio_info(void) {
	printf("Selected PCM device: %s\n", pcm_device);
	printf("Hardware parameters: %s, %d Hz, %d channel%s\n",
			pcm_format_name(pcm_format), pcm_rate,
			pcm_channels, pcm_channels > 1 ? "s" : "");
	if (!signal_meter) {
		printf("Output file type: %s\n", writer_type_to_string(writer->type));
#if ENABLE_MP3LAME
		if (writer->type == WRITER_TYPE_MP3)
			printf("Output bit rate [kbit/s]: min=%d max=%d\n",
					bitrate_min / 1000, bitrate_max / 1000);
#endif
#if ENABLE_VORBIS
		if (writer->type == WRITER_TYPE_OGG)
			printf("Output bit rate [kbit/s]: min=%d nominal=%d max=%d\n",
					bitrate_min / 1000, bitrate_nom / 1000, bitrate_max / 1000);
#endif
	}
}

int main(int argc, char *argv[]) {

	int opt;
	const char *opts = "hVvB:LD:t:c:C:f:r:R:l:o:s:m";
	const struct option longopts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "backend", required_argument, NULL, 'B' },
		{ "list-devices", no_argument, NULL, 'L' },
		{ "device", required_argument, NULL, 'D' },
		{ "file-type", required_argument, NULL, 't' },
		{ "out-format", required_argument, NULL, 't' }, /* old alias */
		{ "channels", required_argument, NULL, 'c' },
		{ "format", required_argument, NULL, 'f' },
		{ "rate", required_argument, NULL, 'r' },
		{ "level", required_argument, NULL, 'l' },
		{ "sig-level", required_argument, NULL, 'l' }, /* old alias */
		{ "fadeout", required_argument, NULL, 'o' },
		{ "fadeout-lag", required_argument, NULL, 'o' }, /* old alias */
		{ "split", required_argument, NULL, 's' },
		{ "split-time", required_argument, NULL, 's' }, /* old alias */
		{ "sig-meter", no_argument, NULL, 'm' },
		{ 0 },
	};

#if ENABLE_ALSA
	enum recorder_type recorder_type = RECORDER_TYPE_ALSA;
#elif ENABLE_PIPEWIRE
	enum recorder_type recorder_type = RECORDER_TYPE_PIPEWIRE;
#elif ENABLE_PORTAUDIO
	enum recorder_type recorder_type = RECORDER_TYPE_PORTAUDIO;
#endif

	/* Select default output format based on available libraries. */
#if ENABLE_SNDFILE
	enum writer_type writer_type = WRITER_TYPE_WAV;
#elif ENABLE_VORBIS
	enum writer_type writer_type = WRITER_TYPE_OGG;
#elif ENABLE_MP3LAME
	enum writer_type writer_type = WRITER_TYPE_MP3;
#else
	enum writer_type writer_type = WRITER_TYPE_RAW;
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
					"  -v, --verbose\t\t\tshow extra information (add more -v for more)\n"
					"  -B, --backend=BACKEND\t\tselect audio backend (current: %s)\n"
					"  -L, --list-devices\t\tlist available audio input devices\n"
					"  -D, --device=DEV\t\tselect audio input device (current: %s)\n"
					"  -t, --file-type=TYPE\t\toutput file type (current: %s)\n"
					"  -c, --channels=NUM\t\tnumber of channels (current: %u)\n"
					"  -f, --format=FORMAT\t\tsample format (current: %s)\n"
					"  -r, --rate=NUM\t\tsample rate (current: %u Hz)\n"
					"  -l, --level=NUM\t\tactivation threshold level (current: %#.1f dB)\n"
					"  -o, --fadeout=SEC\t\tactivation fadeout time (current: %#.1f s)\n"
					"  -s, --split=SEC\t\toutput file split time (current: %#.1f s)\n"
					"  -m, --sig-meter\t\taudio signal level meter\n"
					"\n"
					"The output-template argument is a strftime(3) format string which\n"
					"will be used for creating output file name. If not specified, the\n"
					"default value is: %s + extension\n",
					argv[0],
					recorder_type_to_string(recorder_type),
					pcm_device,
					writer_type_to_string(writer_type),
					pcm_channels,
					pcm_format_name(pcm_format),
					pcm_rate,
					activation_threshold_level_db,
					activation_fadeout_time_ms * 0.001,
					output_split_time_ms * 0.001,
					template);
			return EXIT_SUCCESS;

		case 'V' /* --version */ :
			printf("%s\n", PROJECT_VERSION);
			return EXIT_SUCCESS;

		case 'm' /* --sig-meter */ :
			signal_meter = true;
			break;
		case 'v' /* --verbose */ :
			verbose++;
			break;

		case 'B' /* --backend=BACKEND */ : {

			const enum recorder_type types[] = {
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

			size_t i;
			for (i = 0; i < sizeof(types) / sizeof(*types); i++)
				if (strcasecmp(recorder_type_to_string(types[i]), optarg) == 0) {
					recorder_type = types[i];
					break;
				}

			if (i == sizeof(types) / sizeof(*types)) {
				fprintf(stderr, "error: Unknown audio backend type {");
				for (i = 0; i < sizeof(types) / sizeof(*types); i++) {
					const char * name = recorder_type_to_string(types[i]);
					fprintf(stderr, "%s%s", i != 0 ? ", " : "", name);
				}
				fprintf(stderr, "}: %s\n", optarg);
				return EXIT_FAILURE;
			}

		} break;

		case 'L' /* --list-devices */ :
			list_devices = true;
			break;
		case 'D' /* --device=DEV */ :
			pcm_device = optarg;
			break;

		case 't' /* --file-type=TYPE */ : {

			const enum writer_type types[] = {
				WRITER_TYPE_RAW,
#if ENABLE_MP3LAME
				WRITER_TYPE_MP3,
#endif
#if ENABLE_SNDFILE
				WRITER_TYPE_WAV,
				WRITER_TYPE_RF64,
#endif
#if ENABLE_VORBIS
				WRITER_TYPE_OGG,
#endif
			};

			size_t i;
			for (i = 0; i < sizeof(types) / sizeof(*types); i++)
				if (strcasecmp(writer_type_to_string(types[i]), optarg) == 0) {
					writer_type = types[i];
					break;
				}

			if (i == sizeof(types) / sizeof(*types)) {
				fprintf(stderr, "error: Unknown output file type {");
				for (i = 0; i < sizeof(types) / sizeof(*types); i++) {
					const char * name = writer_type_to_string(types[i]);
					fprintf(stderr, "%s%s", i != 0 ? ", " : "", name);
				}
				fprintf(stderr, "}: %s\n", optarg);
				return EXIT_FAILURE;
			}

		} break;

		case 'c' /* --channels=NUM */ :
		case 'C':
			pcm_channels = abs(atoi(optarg));
			break;
		case 'f' /* --format=FORMAT */ : {

			enum pcm_format formats[] = {
				PCM_FORMAT_U8,
				PCM_FORMAT_S16LE,
			};

			size_t i;
			for (i = 0; i < sizeof(formats) / sizeof(*formats); i++)
				if (strcasecmp(pcm_format_name(formats[i]), optarg) == 0) {
					pcm_format = formats[i];
					break;
				}

			if (i == sizeof(formats) / sizeof(*formats)) {
				fprintf(stderr, "error: Unknown sample format {");
				for (i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
					const char * name = pcm_format_name(formats[i]);
					fprintf(stderr, "%s%s", i != 0 ? ", " : "", name);
				}
				fprintf(stderr, "}: %s\n", optarg);
				return EXIT_FAILURE;
			}

		} break;
		case 'r' /* --rate=NUM */ :
		case 'R':
			pcm_rate = abs(atoi(optarg));
			break;

		case 'l' /* --level=NUM */ :
			activation_threshold_level_db = atof(optarg);
			break;
		case 'o' /* --fadeout=SEC */ :
			activation_fadeout_time_ms = atof(optarg) * 1000;
			break;
		case 's' /* --split=SEC */ :
			output_split_time_ms = atof(optarg) * 1000;
			break;

		default:
			fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
			return EXIT_FAILURE;
		}

	if (optind < argc)
		template = argv[optind];

	switch (recorder_type) {
#if ENABLE_ALSA
	case RECORDER_TYPE_ALSA:
		recorder = recorder_alsa_new(pcm_format, pcm_channels, pcm_rate);
		break;
#endif
#if ENABLE_PIPEWIRE
	case RECORDER_TYPE_PIPEWIRE:
		recorder = recorder_pw_new(pcm_format, pcm_channels, pcm_rate);
		break;
#endif
#if ENABLE_PORTAUDIO
	case RECORDER_TYPE_PORTAUDIO:
		recorder = recorder_pa_new(pcm_format, pcm_channels, pcm_rate);
		break;
#endif
	}

	if (recorder == NULL) {
		error("Couldn't create recorder: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	if (list_devices) {
		recorder_list_devices(recorder);
		recorder_free(recorder);
		return EXIT_SUCCESS;
	}

	if (recorder_open(recorder, pcm_device) == -1)
		/* Error message should be already printed by the backend. */
		return EXIT_FAILURE;

	switch (writer_type) {
	case WRITER_TYPE_RAW:
		writer = writer_raw_new(pcm_format, pcm_channels);
		break;
#if ENABLE_SNDFILE
	case WRITER_TYPE_WAV:
		writer = writer_wav_new(pcm_format, pcm_channels, pcm_rate);
		break;
	case WRITER_TYPE_RF64:
		writer = writer_rf64_new(pcm_format, pcm_channels, pcm_rate);
		break;
#endif
#if ENABLE_MP3LAME
	case WRITER_TYPE_MP3:
		writer = writer_mp3_new(pcm_format, pcm_channels, pcm_rate,
				bitrate_min, bitrate_max, banner);
		if (verbose >= 2)
			writer_mp3_print_internals(writer);
		break;
#endif
#if ENABLE_VORBIS
	case WRITER_TYPE_OGG:
		writer = writer_ogg_new(pcm_format, pcm_channels, pcm_rate,
				bitrate_min, bitrate_nom, bitrate_max, banner);
		break;
#endif
	}

	if (writer == NULL) {
		error("Couldn't create writer: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	if (verbose >= 1)
		print_audio_info();

	struct sigaction sigact = { .sa_handler = main_loop_stop, .sa_flags = SA_RESETHAND };
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);

	recorder->monitor = signal_meter;
	recorder->verbose = verbose;

	if (recorder_start(recorder, writer, template, activation_threshold_level_db,
				activation_fadeout_time_ms, output_split_time_ms) == -1) {
		error("Couldn't start audio recorder: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
