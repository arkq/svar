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

#include "debug.h"
#include "writer.h"
#if ENABLE_MP3LAME
# include "writer-mp3.h"
#endif
#if ENABLE_SNDFILE
# include "writer-wav.h"
#endif
#if ENABLE_VORBIS
# include "writer-ogg.h"
#endif

#define READER_FRAMES 512 * 8
#define PROCESSING_FRAMES READER_FRAMES * 16

/* Application banner used for output file comment string. */
static const char * banner = "SVAR - Simple Voice Activated Recorder";
/* The verbose level used for debugging. */
static int verbose = 0;

/* Selected capturing PCM device. */
#if ENABLE_PORTAUDIO
static int pcm_device_id = 0;
#else
static char pcm_device[25] = "default";
#endif
static unsigned int pcm_channels = 1;
static unsigned int pcm_rate = 44100;

/* If true, run signal meter only. */
static bool signal_meter = false;

/* Selected output writer. */
static struct writer * writer = NULL;
/* The strftime() format template for output file. */
static const char * template = "rec-%d-%H:%M:%S";

/* Variable bit rate settings for writers which support it. */
static int bitrate_min = 32000;
static int bitrate_nom = 64000;
static int bitrate_max = 128000;

/* The signal level threshold for activation (percentage of max signal). */
static int threshold = 2;
/* The fadeout time lag in ms after the last signal peak. */
static int fadeout_time = 500;
/* The split time in seconds for creating new output file. */
static int split_time = 0; /* disable splitting by default */

/* Reader/writer synchronization. */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static bool active = true;
static int16_t * pcm_buffer;
static size_t pcm_buffer_read_len;
static size_t pcm_buffer_size;

static void main_loop_stop(int sig) {
	active = false;
	(void)sig;
}

static const char * get_output_file_name(void) {

	struct tm now;
	const time_t tmp = time(NULL);
	localtime_r(&tmp, &now);

	char base[192];
	static char name[sizeof(base) + 4];
	strftime(base, sizeof(base), template, &now);
	snprintf(name, sizeof(name), "%s.%s",
			base, writer_format_to_string(writer->format));

	return name;
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
	if ((err = snd_pcm_hw_params_set_channels_near(pcm, params, &pcm_channels)) != 0) {
		snprintf(buf, sizeof(buf), "Set channels: %s: %d", snd_strerror(err), pcm_channels);
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_rate_near(pcm, params, &pcm_rate, &dir)) != 0) {
		snprintf(buf, sizeof(buf), "Set sampling rate: %s: %d", snd_strerror(err), pcm_rate);
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

/* Print some information about the audio device and its configuration. */
static void print_audio_info(void) {
#if ENABLE_PORTAUDIO
	printf("Selected PCM device ID: %d\n", pcm_device_id);
#else
	printf("Selected PCM device: %s\n", pcm_device);
#endif
	printf("Hardware parameters: %d Hz, S16LE, %d channel%s\n",
			pcm_rate, pcm_channels, pcm_channels > 1 ? "s" : "");
	if (!signal_meter)
		printf("Output file format: %s\n",
				writer_format_to_string(writer->format));
#if ENABLE_MP3LAME
	if (writer->format == WRITER_FORMAT_MP3)
		printf("Output bit rate [min, max]: %d, %d kbit/s\n",
				bitrate_min / 1000, bitrate_max / 1000);
#endif
#if ENABLE_VORBIS
	if (writer->format == WRITER_FORMAT_OGG)
		printf("Output bit rate [min, nominal, max]: %d, %d, %d kbit/s\n",
				bitrate_min / 1000, bitrate_nom / 1000, bitrate_max / 1000);
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
					i == pcm_device_id ? '*' : ' ',
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

	if (signal_meter) {
		/* dump current peak and RMS values to the stdout */
		printf("\rsignal peak [%%]: %3u, signal RMS [%%]: %3u\r",
				signal_peak * 100 / 0x7ffe, signal_rms * 100 / 0x7ffe);
		fflush(stdout);
		return;
	}

	/* if the max peak in the buffer is greater than the threshold, update
	 * the last peak time */
	if ((int)signal_peak * 100 / 0x7ffe > threshold)
		clock_gettime(CLOCK_MONOTONIC_RAW, &peak_time);

	clock_gettime(CLOCK_MONOTONIC_RAW, &current_time);
	if ((current_time.tv_sec - peak_time.tv_sec) * 1000 +
			(current_time.tv_nsec - peak_time.tv_nsec) / 1000000 < fadeout_time) {

		pthread_mutex_lock(&mutex);

		if (pcm_buffer_read_len == pcm_buffer_size) {
			/* Drop the current buffer, so we can process incoming data. */
			if (verbose >= 1)
				warn("Reader buffer overrun");
			pcm_buffer_read_len = 0;
		}

		/* NOTE: The size of data returned by the pcm_read in the blocking mode is
		 *       always equal to the requested size. So, if the reader buffer (the
		 *       external one) is an integer multiplication of our internal buffer,
		 *       there is no need for any fancy boundary check. However, this might
		 *       not be true if someone is using CPU profiling tool, like cpulimit. */
		memcpy(&pcm_buffer[pcm_buffer_read_len], buffer,
				sizeof(int16_t) * frames * pcm_channels);
		pcm_buffer_read_len += frames * pcm_channels;

		/* dump reader buffer usage */
		debug("Buffer usage: %zd out of %zd", pcm_buffer_read_len, pcm_buffer_size);

		pthread_mutex_unlock(&mutex);

	}

	/* Wake up processing thread to process data if any. */
	pthread_cond_signal(&cond);

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
	process_audio_S16_LE(inputBuffer, framesPerBuffer, pcm_channels);
	return active ? paContinue : paComplete;
}

#else

/* Thread function for ALSA capture. */
static void *alsa_capture_thread(void *arg) {

	int16_t *buffer;
	if ((buffer = malloc(sizeof(int16_t) * pcm_channels * READER_FRAMES)) == NULL) {
		error("Couldn't allocate memory for capturing PCM: %s", strerror(errno));
		return NULL;
	}

	snd_pcm_sframes_t frames;
	snd_pcm_t *pcm = arg;

	while (active) {
		if ((frames = snd_pcm_readi(pcm, buffer, READER_FRAMES)) < 0)
			switch (frames) {
			case -EPIPE:
			case -ESTRPIPE:
				snd_pcm_recover(pcm, frames, 1);
				if (verbose >= 1)
					warn("PCM buffer overrun: %s", snd_strerror(frames));
				continue;
			case -ENODEV:
				error("PCM read error: %s", "Device disconnected");
				goto fail;
			default:
				error("PCM read error: %s", snd_strerror(frames));
				continue;
			}
		process_audio_S16_LE(buffer, frames, pcm_channels);
	}

fail:
	free(buffer);
	return NULL;
}

#endif

static void writer_close(void) {
	if (verbose >= 1)
		info("Closing current output file");
	writer->close(writer);
}

/* Audio signal data processing thread. */
static void *processing_thread(void *arg) {
	(void)arg;

	if (signal_meter)
		return NULL;

	int16_t *buffer = malloc(sizeof(int16_t) * pcm_channels * PROCESSING_FRAMES);
	size_t frames = 0;

	struct timespec ts_last_write = { 0 };
	struct timespec ts_now;

	while (active) {

		pthread_mutex_lock(&mutex);

		while (active && pcm_buffer_read_len == 0) {

			/* Wait for the reader to fill the buffer. */
			pthread_cond_wait(&cond, &mutex);

			if (split_time && writer->opened) {
				/* Check if split time was reached, if so, close writer
				 * and schedule opening of a new one. */
				clock_gettime(CLOCK_MONOTONIC_RAW, &ts_now);
				if (ts_now.tv_sec - ts_last_write.tv_sec > split_time) {
					writer_close();
				}
			}

		}

		/* Copy data from the reader buffer into our internal one. */
		memcpy(buffer, pcm_buffer, sizeof(int16_t) * pcm_buffer_read_len);
		frames = pcm_buffer_read_len / pcm_channels;
		pcm_buffer_read_len = 0;

		pthread_mutex_unlock(&mutex);

		if (frames == 0)
			continue;

		if (!writer->opened) {
			const char * name = get_output_file_name();
			if (verbose >= 1)
				info("Creating new output file: %s", name);
			if (writer->open(writer, name) == -1) {
				error("Couldn't open writer: %s", strerror(errno));
				goto fail;
			}
		}

		clock_gettime(CLOCK_MONOTONIC_RAW, &ts_last_write);
		writer->write(writer, buffer, frames);

	}

fail:
	writer_close();
	writer->free(writer);
	free(buffer);
	return 0;
}

int main(int argc, char *argv[]) {

	int opt;
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

	/* Select default output format based on available libraries. */
#if ENABLE_SNDFILE
	enum writer_format format = WRITER_FORMAT_WAV;
#elif ENABLE_VORBIS
	enum writer_format format = WRITER_FORMAT_OGG;
#elif ENABLE_MP3LAME
	enum writer_format format = WRITER_FORMAT_MP3;
#else
	enum writer_format format = WRITER_FORMAT_RAW;
#endif

#if ENABLE_PORTAUDIO
	PaError pa_err;
	if ((pa_err = Pa_Initialize()) != paNoError) {
		error("Couldn't initialize PortAudio: %s", Pa_GetErrorText(pa_err));
		return EXIT_FAILURE;
	}
	if ((pcm_device_id = Pa_GetDefaultInputDevice()) == paNoDevice)
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
					pcm_device_id,
#else
					pcm_device,
#endif
					pcm_rate,
					pcm_channels,
					threshold,
					fadeout_time,
					split_time,
					writer_format_to_string(format),
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

#if ENABLE_PORTAUDIO
		case 'L' /* --list-devices */ :
			pa_list_devices();
			return EXIT_SUCCESS;
		case 'D' /* --device=ID */ :
			pcm_device_id = atoi(optarg);
			break;
#else
		case 'D' /* --device=DEV */ :
			strncpy(pcm_device, optarg, sizeof(pcm_device) - 1);
			break;
#endif

		case 'C' /* --channels */ :
			pcm_channels = abs(atoi(optarg));
			break;
		case 'R' /* --rate */ :
			pcm_rate = abs(atoi(optarg));
			break;

		case 'o' /* --out-format */ : {

			const enum writer_format formats[] = {
				WRITER_FORMAT_RAW,
#if ENABLE_MP3LAME
				WRITER_FORMAT_MP3,
#endif
#if ENABLE_SNDFILE
				WRITER_FORMAT_WAV,
#endif
#if ENABLE_VORBIS
				WRITER_FORMAT_OGG,
#endif
			};

			size_t i;
			for (i = 0; i < sizeof(formats) / sizeof(*formats); i++)
				if (strcasecmp(writer_format_to_string(formats[i]), optarg) == 0) {
					format = formats[i];
					break;
				}

			if (i == sizeof(formats) / sizeof(*formats)) {
				fprintf(stderr, "error: Unknown output format {");
				for (i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
					const char * name = writer_format_to_string(formats[i]);
					fprintf(stderr, "%s%s", i != 0 ? ", " : "", name);
				}
				fprintf(stderr, "}: %s\n", optarg);
				return EXIT_FAILURE;
			}

		} break;

		case 'l' /* --sig-level */ :
			threshold = atoi(optarg);
			if (threshold < 0 || threshold > 100) {
				error("Signal level out of range [0, 100]: %d", threshold);
				return EXIT_FAILURE;
			}
			break;
		case 'f' /* --fadeout-lag */ :
			fadeout_time = atoi(optarg);
			if (fadeout_time < 100 || fadeout_time > 1000000) {
				error("Fadeout lag out of range [100, 1000000]: %d", fadeout_time);
				return EXIT_FAILURE;
			}
			break;
		case 's' /* --split-time */ :
			split_time = atoi(optarg);
			if (split_time < 0 || split_time > 1000000) {
				error("Split time out of range [0, 1000000]: %d", split_time);
				return EXIT_FAILURE;
			}
			break;

		default:
			fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
			return EXIT_FAILURE;
		}

	if (optind < argc)
		template = argv[optind];

#if !ENABLE_PORTAUDIO
	pthread_t thread_alsa_capture_id;
#endif
	pthread_t thread_process_id;
	int err;

	/* initialize reader data */
	pcm_buffer_size = pcm_channels * PROCESSING_FRAMES;
	pcm_buffer = malloc(sizeof(int16_t) * pcm_buffer_size);
	pcm_buffer_read_len = 0;

	if (pcm_buffer == NULL) {
		error("Couldn't create reader buffer: %s", strerror(errno));
		return EXIT_FAILURE;
	}

#if ENABLE_PORTAUDIO

	PaStream *pa_stream = NULL;
	PaStreamParameters pa_params = {
		.sampleFormat = paInt16,
		.device = pcm_device_id,
		.channelCount = pcm_channels,
		.suggestedLatency = Pa_GetDeviceInfo(pcm_device_id)->defaultLowInputLatency,
		.hostApiSpecificStreamInfo = NULL,
	};

	if ((pa_err = Pa_OpenStream(&pa_stream, &pa_params, NULL, pcm_rate,
					READER_FRAMES, paClipOff, pa_capture_callback, NULL)) != paNoError) {
		error("Couldn't open PortAudio stream: %s", Pa_GetErrorText(pa_err));
		return EXIT_FAILURE;
	}

#else

	snd_pcm_t *pcm;
	char *msg;

	if ((err = snd_pcm_open(&pcm, pcm_device, SND_PCM_STREAM_CAPTURE, 0)) != 0) {
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

	switch (format) {
	case WRITER_FORMAT_RAW:
		writer = writer_raw_new(pcm_channels);
		break;
#if ENABLE_SNDFILE
	case WRITER_FORMAT_WAV:
		writer = writer_wav_new(pcm_channels, pcm_rate);
		break;
#endif
#if ENABLE_MP3LAME
	case WRITER_FORMAT_MP3:
		writer = writer_mp3_new(pcm_channels, pcm_rate,
				bitrate_min, bitrate_max, banner);
		if (verbose >= 2)
			writer_mp3_print_internals(writer);
		break;
#endif
#if ENABLE_VORBIS
	case WRITER_FORMAT_OGG:
		writer = writer_ogg_new(pcm_channels, pcm_rate,
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
	pthread_mutex_lock(&mutex);
	active = false;
	pthread_mutex_unlock(&mutex);
	pthread_cond_signal(&cond);

	pthread_join(thread_process_id, NULL);

	if (signal_meter)
		printf("\n");

	return EXIT_SUCCESS;
}
