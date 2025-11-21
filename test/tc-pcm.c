/*
 * SVAR - tc-pcm.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include <check.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "pcm.h"

START_TEST(test_pcm_format_name) {

	ck_assert_str_eq(pcm_format_name(PCM_FORMAT_U8), "U8");
	ck_assert_str_eq(pcm_format_name(PCM_FORMAT_S16LE), "S16LE");
	ck_assert_str_eq(pcm_format_name(-1), "INVALID");

} END_TEST

START_TEST(test_pcm_format_size) {

	ck_assert_uint_eq(pcm_format_size(PCM_FORMAT_U8, 1024), 1024);
	ck_assert_uint_eq(pcm_format_size(PCM_FORMAT_S16LE, 1024), 2048);
	ck_assert_uint_eq(pcm_format_size(-1, 1024), 0);

} END_TEST

START_TEST(test_pcm_rms_db) {

	uint8_t buffer_u8[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
	int16_t buffer_s16le[8] = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 };

	ck_assert_double_eq(pcm_rms_db(PCM_FORMAT_U8, buffer_u8, 0), -96.0);
	ck_assert_double_eq(round(pcm_rms_db(PCM_FORMAT_U8, buffer_u8, 8) * 100), -338.0);

	ck_assert_double_eq(pcm_rms_db(PCM_FORMAT_S16LE, buffer_s16le, 0), -96.0);
	ck_assert_double_eq(round(pcm_rms_db(PCM_FORMAT_S16LE, buffer_s16le, 8) * 100), -1624.0);

} END_TEST

int tcase_init(Suite * s) {

	TCase * tc = tcase_create(__FILE__);
	suite_add_tcase(s, tc);

	tcase_add_test(tc, test_pcm_format_name);
	tcase_add_test(tc, test_pcm_format_size);
	tcase_add_test(tc, test_pcm_rms_db);

	return 0;
}
