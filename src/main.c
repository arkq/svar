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
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>

#if ENABLE_PIPEWIRE
# include <pipewire/pipewire.h>
# include <spa/param/audio/format-utils.h>
# include <spa/utils/result.h>
#elif ENABLE_PORTAUDIO
# include <portaudio.h>
#else
# include <alsa/asoundlib.h>
#endif

#include "log.h"
#include "pcm.h"
#include "rbuf.h"
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

/* Application banner used for output file comment string. */
static const char * banner = "SVAR - Simple Voice Activated Recorder";
/* The verbose level used for debugging. */
static int verbose = 0;

/* Selected capturing PCM device. */
#if ENABLE_PORTAUDIO
static int pcm_device_id = 0;
#else
static char pcm_device[64] = "default";
#endif
static enum pcm_format pcm_format = PCM_FORMAT_S16LE;
static unsigned int pcm_channels = 1;
static unsigned int pcm_rate = 44100;
/* The number of frames read in a single read operation. This value
 * should be set to pcm_rate / 10 to get 100 ms of audio data. */
static size_t pcm_read_frames = 4410;

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

/* The activation threshold level in dB. */
static double activation_threshold_level = -50.0;
/* The activation fadeout time - the time after the last activation signal. */
static long activation_fadeout_time_ms = 500;
/* The split time in seconds for creating new output file. */
static long output_split_time_ms = 0; /* disable splitting by default */

/* Reader/writer synchronization. */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

#if ENABLE_PIPEWIRE
static struct pw_main_loop * pw_main_loop = NULL;
#endif

static bool active = true;
static struct rbuf rb;

static void main_loop_stop(int sig) {
	active = false;
#if ENABLE_PIPEWIRE
	pw_main_loop_quit(pw_main_loop);
#endif
	(void)sig;
}

static time_t ts_diff_ms(const struct timespec * ts1, const struct timespec * ts2) {
	return (ts1->tv_sec - ts2->tv_sec) * 1000 + (ts1->tv_nsec - ts2->tv_nsec) / 1000000;
}

static const char * output_file_name(void) {

	struct tm now;
	const time_t tmp = time(NULL);
	localtime_r(&tmp, &now);

	char base[192];
	static char name[sizeof(base) + 4];
	strftime(base, sizeof(base), template, &now);
	snprintf(name, sizeof(name), "%s.%s",
			base, writer_type_to_string(writer->type));

	return name;
}

#if !ENABLE_PIPEWIRE && !ENABLE_PORTAUDIO
/* Set ALSA hardware parameters. */
static int pcm_set_hw_params(snd_pcm_t *pcm, char **msg) {

	static const snd_pcm_format_t pcm_format_mapping[] = {
		[PCM_FORMAT_U8] = SND_PCM_FORMAT_U8,
		[PCM_FORMAT_S16LE] = SND_PCM_FORMAT_S16_LE,
	};

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
	const snd_pcm_format_t format = pcm_format_mapping[pcm_format];
	if ((err = snd_pcm_hw_params_set_format(pcm, params, format)) != 0) {
		snprintf(buf, sizeof(buf), "Set format: %s: %s",
				snd_strerror(err), snd_pcm_format_name(format));
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

static bool check_activation_threshold(const void * buffer, size_t samples) {

	static struct timespec activation_time = { 0 };
	double buffer_rms_db = pcm_rms_db(pcm_format, buffer, samples);

	if (signal_meter) {
		/* Dump current RMS values to the stdout. */
		printf("\rSignal RMS: %#5.1f dB\r", buffer_rms_db);
		fflush(stdout);
		return false;
	}

	/* Update time if the signal RMS in the buffer exceeds the threshold. */
	if (buffer_rms_db > activation_threshold_level)
		clock_gettime(CLOCK_MONOTONIC_RAW, &activation_time);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	if (ts_diff_ms(&now, &activation_time) < activation_fadeout_time_ms)
		return true;

	return false;
}

#if ENABLE_PIPEWIRE || ENABLE_PORTAUDIO
static void process_audio(const void * buffer, size_t samples) {

	if (!check_activation_threshold(buffer, samples))
		return;

	while (samples > 0) {

		pthread_mutex_lock(&mutex);

		const size_t rb_wr_capacity_samples = rbuf_write_linear_capacity(&rb);
		const size_t batch_samples = MIN(samples, rb_wr_capacity_samples);
		const size_t batch_sample_bytes = pcm_format_size(pcm_format, batch_samples);

		if (rb_wr_capacity_samples == 0) {
			if (verbose >= 1)
				warn("PCM buffer overrun: %s", "Ring buffer full");
			pthread_mutex_unlock(&mutex);
			break;
		}

		memcpy(rb.tail, buffer, batch_sample_bytes);
		rbuf_write_linear_commit(&rb, batch_samples);

		buffer = (const unsigned char *)buffer + batch_sample_bytes;
		samples -= batch_samples;

		pthread_mutex_unlock(&mutex);

		/* Wake up the processing thread to process audio or to close
		 * the current writer if split time was exceeded. */
		pthread_cond_signal(&cond);

	}

}
#endif

#if ENABLE_PIPEWIRE

/* Callback function for PipeWire capture. */
static void pw_capture_callback(void * data) {
	struct pw_stream ** stream = data;

	struct pw_buffer * b;
	if ((b = pw_stream_dequeue_buffer(*stream)) == NULL) {
		warn("Couldn't dequeue PipeWire capture buffer");
		return;
	}

	struct spa_buffer * buf = b->buffer;
	struct spa_data * d = &buf->datas[0];

	if (d->data == NULL)
		return;

	process_audio(
			SPA_PTROFF(d->data, d->chunk->offset, void),
			d->chunk->size / pcm_format_size(pcm_format, 1));

	pw_stream_queue_buffer(*stream, b);

}

#elif ENABLE_PORTAUDIO

/* Callback function for PortAudio capture. */
static int pa_capture_callback(const void * inputBuffer, void * outputBuffer,
		unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo * timeInfo,
		PaStreamCallbackFlags statusFlags, void * userData) {
	(void)outputBuffer;
	(void)timeInfo;
	(void)statusFlags;
	(void)userData;
	process_audio(inputBuffer, framesPerBuffer * pcm_channels);
	return active ? paContinue : paComplete;
}

#else

/* Thread function for ALSA capture. */
static void *alsa_capture_thread(void *arg) {
	snd_pcm_t * pcm = arg;

	while (active) {

		pthread_mutex_lock(&mutex);
		const size_t samples = rbuf_write_linear_capacity(&rb);
		pthread_mutex_unlock(&mutex);

		snd_pcm_sframes_t frames = samples / pcm_channels;
		if ((frames = snd_pcm_readi(pcm, rb.tail, MIN(frames, pcm_read_frames))) < 0)
			switch (frames) {
			case -EPIPE:
			case -ESTRPIPE:
				snd_pcm_recover(pcm, frames, 1);
				if (verbose >= 1)
					warn("PCM buffer overrun: %s", snd_strerror(frames));
				continue;
			case -ENODEV:
				error("PCM read error: %s", "Device disconnected");
				return NULL;
			default:
				error("PCM read error: %s", snd_strerror(frames));
				continue;
			}

		if (check_activation_threshold(rb.tail, frames * pcm_channels)) {
			pthread_mutex_lock(&mutex);
			rbuf_write_linear_commit(&rb, frames * pcm_channels);
			pthread_mutex_unlock(&mutex);
		}

		/* Wake up the processing thread to process audio or to close
		 * the current writer if split time was exceeded. */
		pthread_cond_signal(&cond);

	}

	return NULL;
}

#endif

/* Audio signal data processing thread. */
static void *processing_thread(void *arg) {
	(void)arg;

	if (signal_meter)
		return NULL;

	struct timespec ts_last_write = { 0 };
	struct timespec ts_now;
	size_t samples = 0;

	while (true) {

		pthread_mutex_lock(&mutex);

		while (active && (samples = rbuf_read_linear_capacity(&rb)) == 0) {
			/* Wait for the reader to fill the buffer. */
			pthread_cond_wait(&cond, &mutex);
			/* If split time is enabled and writer is opened,
			 * check if we need to close the current writer. */
			if (output_split_time_ms && writer->opened) {
				clock_gettime(CLOCK_MONOTONIC_RAW, &ts_now);
				if (ts_diff_ms(&ts_now, &ts_last_write) > output_split_time_ms) {
					if (verbose >= 1)
						info("Closing current output file");
					writer->close(writer);
				}
			}
		}

		pthread_mutex_unlock(&mutex);

		if (!active)
			break;

		if (!writer->opened) {
			const char * name = output_file_name();
			if (verbose >= 1)
				info("Creating new output file: %s", name);
			if (writer->open(writer, name) == -1) {
				error("Couldn't open writer: %s", strerror(errno));
				goto fail;
			}
		}

		clock_gettime(CLOCK_MONOTONIC_RAW, &ts_last_write);
		writer->write(writer, rb.head, samples / pcm_channels);

		pthread_mutex_lock(&mutex);
		rbuf_read_linear_commit(&rb, samples);
		pthread_mutex_unlock(&mutex);

	}

fail:
	if (writer->opened && verbose >= 1)
		info("Closing current output file");
	writer->free(writer);
	return 0;
}

int main(int argc, char *argv[]) {

	int opt;
	const char *opts = "hVvLD:t:c:C:f:r:R:l:o:s:m";
	const struct option longopts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
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

	/* Select default output format based on available libraries. */
#if ENABLE_SNDFILE
	enum writer_type type = WRITER_TYPE_WAV;
#elif ENABLE_VORBIS
	enum writer_type type = WRITER_TYPE_OGG;
#elif ENABLE_MP3LAME
	enum writer_type type = WRITER_TYPE_MP3;
#else
	enum writer_type type = WRITER_TYPE_RAW;
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
					"  -v, --verbose\t\t\tshow extra information (add more -v for more)\n"
#if ENABLE_PORTAUDIO
					"  -L, --list-devices\t\tlist available audio input devices\n"
					"  -D, --device=ID\t\tselect audio input device (current: %d)\n"
#else
					"  -D, --device=DEV\t\tselect audio input device (current: %s)\n"
#endif
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
#if ENABLE_PORTAUDIO
					pcm_device_id,
#else
					pcm_device,
#endif
					writer_type_to_string(type),
					pcm_channels,
					pcm_format_name(pcm_format),
					pcm_rate,
					activation_threshold_level,
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
					type = types[i];
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
			pcm_read_frames = pcm_rate / 10;
			break;

		case 'l' /* --level=NUM */ :
			activation_threshold_level = atof(optarg);
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

#if !ENABLE_PIPEWIRE && !ENABLE_PORTAUDIO
	pthread_t thread_alsa_capture_id;
#endif
	pthread_t thread_process_id;
	int err;

#if ENABLE_PIPEWIRE

	static const enum spa_audio_format pw_format_mapping[] = {
		[PCM_FORMAT_U8] = SPA_AUDIO_FORMAT_U8,
		[PCM_FORMAT_S16LE] = SPA_AUDIO_FORMAT_S16_LE,
	};

	static const struct pw_stream_events pw_stream_events = {
		PW_VERSION_STREAM_EVENTS,
		.process = pw_capture_callback,
	};

	uint8_t buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	pw_init(NULL, NULL);

	if ((pw_main_loop = pw_main_loop_new(NULL)) == NULL) {
		error("Couldn't create PipeWire main loop");
		return EXIT_FAILURE;
	}

	struct pw_properties * props = pw_properties_new(
			PW_KEY_MEDIA_TYPE, "Audio",
			PW_KEY_MEDIA_CATEGORY, "Capture",
			PW_KEY_MEDIA_ROLE, "DSP",
			PW_KEY_TARGET_OBJECT, pcm_device,
			NULL);

	struct pw_stream * stream;
	stream = pw_stream_new_simple(pw_main_loop_get_loop(pw_main_loop), "svar",
			props, &pw_stream_events, &stream);

	const struct spa_pod * params[1];
	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
			&SPA_AUDIO_INFO_RAW_INIT(
				.format = pw_format_mapping[pcm_format],
				.channels = pcm_channels,
				.rate = pcm_rate));

	int ret;
	if ((ret = pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
					PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS,
					params, 1)) < 0) {
		error("Couldn't connect PipeWire stream: %s", spa_strerror(ret));
		return EXIT_FAILURE;
	}

#elif ENABLE_PORTAUDIO

	static const PaSampleFormat pa_pcm_format_mapping[] = {
		[PCM_FORMAT_U8] = paUInt8,
		[PCM_FORMAT_S16LE] = paInt16,
	};

	PaStream *pa_stream = NULL;
	PaStreamParameters pa_params = {
		.device = pcm_device_id,
		.channelCount = pcm_channels,
		.sampleFormat = pa_pcm_format_mapping[pcm_format],
		.suggestedLatency = Pa_GetDeviceInfo(pcm_device_id)->defaultLowInputLatency,
		.hostApiSpecificStreamInfo = NULL,
	};

	if ((pa_err = Pa_OpenStream(&pa_stream, &pa_params, NULL, pcm_rate,
					pcm_read_frames, paClipOff, pa_capture_callback, NULL)) != paNoError) {
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

	switch (type) {
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

	const size_t rb_nmemb = pcm_channels * pcm_read_frames * 8;
	if (rbuf_init(&rb, rb_nmemb, pcm_format_size(pcm_format, 1)) != 0) {
		error("Couldn't create ring buffer: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	struct sigaction sigact = { .sa_handler = main_loop_stop, .sa_flags = SA_RESETHAND };
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);

#if ENABLE_PIPEWIRE
	/* PipeWire stream will start automatically when connected. */
#elif ENABLE_PORTAUDIO
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

#if ENABLE_PIPEWIRE
	pw_main_loop_run(pw_main_loop);
#elif ENABLE_PORTAUDIO
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
