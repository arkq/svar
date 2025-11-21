/*
 * SVAR - tc-recorder.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <check.h>
#include <stdint.h>

#include "recorder.h"
#include "recorder-alsa.h"
#include "recorder-pipewire.h"
#include "recorder-portaudio.h"

#if ENABLE_ALSA
START_TEST(test_recorder_alsa) {

	struct recorder * rec;
	ck_assert_ptr_nonnull(rec = recorder_alsa_new(PCM_FORMAT_S16LE, 2, 44100));
	ck_assert_uint_eq(rec->type, RECORDER_TYPE_ALSA);

	recorder_list_devices(rec);
	recorder_free(rec);

} END_TEST
#endif

#if ENABLE_PIPEWIRE
START_TEST(test_recorder_pipewire) {

	struct recorder * rec;
	ck_assert_ptr_nonnull(rec = recorder_pw_new(PCM_FORMAT_S16LE, 2, 44100));
	ck_assert_uint_eq(rec->type, RECORDER_TYPE_PIPEWIRE);

	recorder_list_devices(rec);
	recorder_free(rec);

} END_TEST
#endif

#if ENABLE_PORTAUDIO
START_TEST(test_recorder_portaudio) {

	struct recorder * rec;
	ck_assert_ptr_nonnull(rec = recorder_pa_new(PCM_FORMAT_S16LE, 2, 44100));
	ck_assert_uint_eq(rec->type, RECORDER_TYPE_PORTAUDIO);

	recorder_list_devices(rec);
	recorder_free(rec);

} END_TEST
#endif

START_TEST(test_recorder_monitor) {

	struct recorder * rec;
	ck_assert_ptr_nonnull(rec = recorder_new(PCM_FORMAT_U8, 1, 44100));
	rec->monitor = true;

	uint8_t pcm[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 };
	ck_assert_int_eq(recorder_monitor(rec, pcm, sizeof(pcm)), -2);

	recorder_free(rec);

} END_TEST

START_TEST(test_recorder_process) {

	struct recorder * rec;
	ck_assert_ptr_nonnull(rec = recorder_new(PCM_FORMAT_U8, 1, 44100));
	rec->activation_threshold_level_db = -42.0;

	uint8_t pcm[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 };
	ck_assert_int_eq(recorder_process(rec, pcm, sizeof(pcm)), 0);
	ck_assert_uint_eq(rbuf_read_linear_capacity(&rec->rb), sizeof(pcm));

	recorder_free(rec);

} END_TEST

START_TEST(test_recorder_type_to_string) {
#if ENABLE_ALSA
	ck_assert_str_eq(recorder_type_to_string(RECORDER_TYPE_ALSA), "ALSA");
#endif
#if ENABLE_PIPEWIRE
	ck_assert_str_eq(recorder_type_to_string(RECORDER_TYPE_PIPEWIRE), "PipeWire");
#endif
#if ENABLE_PORTAUDIO
	ck_assert_str_eq(recorder_type_to_string(RECORDER_TYPE_PORTAUDIO), "PortAudio");
#endif
} END_TEST

int tcase_init(Suite * s) {

	TCase * tc = tcase_create(__FILE__);
	suite_add_tcase(s, tc);

#if ENABLE_ALSA
	tcase_add_test(tc, test_recorder_alsa);
#endif

#if ENABLE_PIPEWIRE
	tcase_add_test(tc, test_recorder_pipewire);
#endif

#if ENABLE_PORTAUDIO
	tcase_add_test(tc, test_recorder_portaudio);
#endif

	tcase_add_test(tc, test_recorder_monitor);
	tcase_add_test(tc, test_recorder_process);
	tcase_add_test(tc, test_recorder_type_to_string);

	return 0;
}
