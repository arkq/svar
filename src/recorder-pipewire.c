/*
 * SVAR - recorder-pipewire.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "recorder-pipewire.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include "log.h"
#include "pcm.h"
#include "recorder.h"

struct recorder_pw {
	struct pw_main_loop * loop;
	struct pw_stream * stream;
	/* Sequence number for the sync event. */
	int seq;
};

static const enum spa_audio_format pcm_format_mapping[] = {
	[PCM_FORMAT_U8] = SPA_AUDIO_FORMAT_U8,
	[PCM_FORMAT_S16LE] = SPA_AUDIO_FORMAT_S16_LE,
};

static void pw_global_callback(void * data, uint32_t id, uint32_t permissions,
		const char * type, uint32_t version, const struct spa_dict * props) {
	(void)data;
	(void)permissions;
	(void)version;

	const char * v;

	if (strcmp(type, PW_TYPE_INTERFACE_Client) == 0) {
		printf("%u", id);
			if ((v = spa_dict_lookup(props, "application.name")) != NULL)
				printf(" / %s", v);
		printf("\n");
	}

	if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
		if ((v = spa_dict_lookup(props, "media.class")) != NULL &&
				strcmp(v, "Audio/Source") == 0) {
			printf("%u", id);
			if ((v = spa_dict_lookup(props, "node.name")) != NULL)
				printf(" / %s", v);
			if ((v = spa_dict_lookup(props, "node.nick")) != NULL)
				printf(" / %s", v);
			printf("\n");
			if ((v = spa_dict_lookup(props, "node.description")) != NULL)
				printf("    %s\n", v);
		}
	}

}

static const struct pw_registry_events pw_registry_events = {
	PW_VERSION_REGISTRY_EVENTS,
	.global = pw_global_callback,
};

static void pw_done_callback(void * data, uint32_t id, int seq) {
	struct recorder_pw * rr = data;
	if (id == PW_ID_CORE && seq == rr->seq)
		pw_main_loop_quit(rr->loop);
}

static const struct pw_core_events pw_core_events = {
	PW_VERSION_CORE_EVENTS,
	.done = pw_done_callback,
};

static void recorder_pw_list(struct recorder * r) {
	struct recorder_pw * rr = r->r;

	struct pw_context * context;
	if ((context = pw_context_new(pw_main_loop_get_loop(rr->loop), NULL, 0)) == NULL) {
		error("Couldn't create PipeWire context");
		return;
	}

	struct pw_core * core;
	if ((core = pw_context_connect(context, NULL, 0)) == NULL) {
		error("Couldn't connect to PipeWire server");
		pw_context_destroy(context);
		return;
	}

	struct pw_registry * registry;
	if ((registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0)) == NULL) {
		error("Couldn't get PipeWire registry");
		pw_core_disconnect(core);
		pw_context_destroy(context);
		return;
	}

	struct spa_hook listener1;
	pw_core_add_listener(core, &listener1, &pw_core_events, rr);
	struct spa_hook listener2;
	pw_registry_add_listener(registry, &listener2, &pw_registry_events, NULL);

	rr->seq = pw_core_sync(core, PW_ID_CORE, 0);
	pw_main_loop_run(rr->loop);

	pw_proxy_destroy((struct pw_proxy *)registry);
	pw_core_disconnect(core);
	pw_context_destroy(context);

}

static void recorder_pw_free(struct recorder * r) {
	struct recorder_pw * rr = r->r;
	if (rr->stream != NULL)
		pw_stream_destroy(rr->stream);
	if (rr->loop != NULL)
		pw_main_loop_destroy(rr->loop);
	pw_deinit();
	free(rr);
}

static void pw_capture_callback(void * data) {
	struct recorder * r = data;
	struct recorder_pw * rr = r->r;

	struct pw_buffer * b;
	if ((b = pw_stream_dequeue_buffer(rr->stream)) == NULL) {
		warn("Couldn't dequeue PipeWire capture buffer");
		return;
	}

	struct spa_buffer * buf = b->buffer;
	struct spa_data * d = &buf->datas[0];

	if (d->data == NULL)
		return;

	recorder_process(r,
			SPA_PTROFF(d->data, d->chunk->offset, void),
			d->chunk->size / pcm_format_size(r->format, 1));

	pw_stream_queue_buffer(rr->stream, b);

}

static const struct pw_stream_events pw_stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.process = pw_capture_callback,
};

static int recorder_pw_open(struct recorder * r, const char * device) {
	struct recorder_pw * rr = r->r;

	uint8_t buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	struct pw_properties * props = pw_properties_new(
			PW_KEY_MEDIA_TYPE, "Audio",
			PW_KEY_MEDIA_CATEGORY, "Capture",
			PW_KEY_MEDIA_ROLE, "DSP",
			PW_KEY_TARGET_OBJECT, device,
			NULL);

	rr->stream = pw_stream_new_simple(pw_main_loop_get_loop(rr->loop), "svar",
			props, &pw_stream_events, r);

	const struct spa_pod * params[1];
	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
			&SPA_AUDIO_INFO_RAW_INIT(
				.format = pcm_format_mapping[r->format],
				.channels = r->channels,
				.rate = r->rate));

	int ret;
	if ((ret = pw_stream_connect(rr->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
					PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS,
					params, 1)) < 0) {
		error("Couldn't connect PipeWire stream: %s", spa_strerror(ret));
		goto fail;
	}

	return 0;

fail:
	recorder_pw_free(r);
	return -1;
}

static int recorder_pw_start(struct recorder * r) {
	struct recorder_pw * rr = r->r;
	/* PipeWire stream will start automatically when connected. */
	pw_main_loop_run(rr->loop);
	return 0;
}

static void recorder_pw_stop(struct recorder * r) {
	struct recorder_pw * rr = r->r;
	pw_main_loop_quit(rr->loop);
}

struct recorder * recorder_pw_new(
		enum pcm_format format, unsigned int channels, unsigned int rate) {

	struct recorder * r;
	if ((r = recorder_new(format, channels, rate)) == NULL)
		return NULL;

	r->type = RECORDER_TYPE_PIPEWIRE;
	r->open = recorder_pw_open;
	r->start = recorder_pw_start;
	r->stop = recorder_pw_stop;
	r->list = recorder_pw_list;
	r->free = recorder_pw_free;

	if ((r->r = calloc(1, sizeof(struct recorder_pw))) == NULL) {
		recorder_free(r);
		return NULL;
	}

	pw_init(NULL, NULL);

	struct recorder_pw * rr = r->r;
	if ((rr->loop = pw_main_loop_new(NULL)) == NULL) {
		recorder_free(r);
		return NULL;
	}

	return r;
}
