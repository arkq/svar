/*
 * SVAR - recorder-portaudio.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "recorder-portaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <portaudio.h>

#include "log.h"
#include "pcm.h"
#include "recorder.h"

static const PaSampleFormat pcm_format_mapping[] = {
	[PCM_FORMAT_U8] = paUInt8,
	[PCM_FORMAT_S16LE] = paInt16,
};

static void recorder_pa_list(struct recorder * r) {
	(void)r;

	PaDeviceIndex count = Pa_GetDeviceCount();
	int default_device_id = Pa_GetDefaultInputDevice();

	for (PaDeviceIndex i = 0; i < count; i++) {
		const PaDeviceInfo * info;
		if ((info = Pa_GetDeviceInfo(i))->maxInputChannels > 0)
			printf("%d%s\n    %s\n",
					i, i == default_device_id ? " / default" : "",
					info->name);
	}

}

static void recorder_pa_free(struct recorder * r) {
	PaStream * stream = r->r;

	PaError err;

	if (stream != NULL) {
		if ((err = Pa_StopStream(stream)) != paNoError)
			warn("Couldn't stop PortAudio stream: %s", Pa_GetErrorText(err));
		if ((err = Pa_CloseStream(stream)) != paNoError)
			warn("Couldn't close PortAudio stream: %s", Pa_GetErrorText(err));
	}

	if ((err = Pa_Terminate()) != paNoError)
		warn("Couldn't terminate PortAudio: %s", Pa_GetErrorText(err));

}

static int pa_capture_callback(const void * inputBuffer, void * outputBuffer,
		unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo * timeInfo,
		PaStreamCallbackFlags statusFlags, void * userData) {
	struct recorder * r = userData;
	(void)outputBuffer;
	(void)timeInfo;
	(void)statusFlags;
	recorder_process(r, inputBuffer, framesPerBuffer * r->channels);
	return r->started ? paContinue : paComplete;
}

static int recorder_pa_open(struct recorder * r, const char * device) {

	int device_id;
	if (strcmp(device, "default") == 0)
		device_id = Pa_GetDefaultInputDevice();
	else {
		char * endptr;
		device_id = strtol(device, &endptr, 10);
		if (endptr == device)
			device_id = paNoDevice;
	}

	const PaDeviceInfo * device_info;
	if ((device_info = Pa_GetDeviceInfo(device_id)) == NULL) {
		error("Invalid PortAudio device ID: %s", device);
		goto fail;
	}

	PaStreamParameters params = {
		.device = device_id,
		.channelCount = r->channels,
		.sampleFormat = pcm_format_mapping[r->format],
		.suggestedLatency = device_info->defaultLowInputLatency,
		.hostApiSpecificStreamInfo = NULL,
	};

	PaError err;
	PaStream * stream = NULL;
	const size_t pcm_read_frames = r->rate / 10;
	if ((err = Pa_OpenStream(&stream, &params, NULL, r->rate,
					pcm_read_frames, paClipOff, pa_capture_callback, r)) != paNoError) {
		error("Couldn't open PortAudio stream: %s", Pa_GetErrorText(err));
		goto fail;
	}

	r->r = stream;

	return 0;

fail:
	recorder_pa_free(r);
	return -1;
}

static int recorder_pa_start(struct recorder * r) {
	PaStream * stream = r->r;

	PaError err;
	if ((err = Pa_StartStream(stream)) != paNoError) {
		error("Couldn't start PortAudio stream: %s", Pa_GetErrorText(err));
		return -1;
	}

	while ((err = Pa_IsStreamActive(stream)) == 1)
		Pa_Sleep(1000);

	if (err < 0)
		error("Couldn't check PortAudio activity: %s", Pa_GetErrorText(err));

	return 0;
}

static void recorder_pa_stop(struct recorder * r) {
	(void)r;
}

struct recorder * recorder_pa_new(
		enum pcm_format format, unsigned int channels, unsigned int rate) {

	struct recorder * r;
	if ((r = recorder_new(format, channels, rate)) == NULL)
		return NULL;

	r->type = RECORDER_TYPE_PORTAUDIO;
	r->open = recorder_pa_open;
	r->start = recorder_pa_start;
	r->stop = recorder_pa_stop;
	r->list = recorder_pa_list;
	r->free = recorder_pa_free;

	if (Pa_Initialize() != paNoError) {
		recorder_free(r);
		return NULL;
	}

	return r;
}
