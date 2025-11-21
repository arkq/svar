/*
 * SVAR - tc-writer.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "writer.h"
#include "writer-mp3.h"
#include "writer-wav.h"
#include "writer-opus.h"
#include "writer-vorbis.h"

static const uint8_t pcm_u8[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
static const int16_t pcm_s16[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

START_TEST(test_writer_raw) {

	struct writer * w;
	ck_assert_ptr_ne(w = writer_raw_new(PCM_FORMAT_U8, 1), NULL);
	ck_assert_uint_eq(w->type, WRITER_TYPE_RAW);
	ck_assert_uint_eq(w->opened, false);

	w->close(w);
	w->free(w);

} END_TEST

START_TEST(test_writer_raw_write) {

	struct writer * w;
	const char * filename = "tc-writer.raw";
	ck_assert_ptr_ne(w = writer_raw_new(PCM_FORMAT_U8, 1), NULL);
	ck_assert_int_ne(w->open(w, filename), -1);
	ck_assert_uint_eq(w->opened, true);

	w->write(w, &pcm_u8[0], 5);
	w->write(w, &pcm_u8[5], 5);
	w->close(w);

	FILE * f;
	uint8_t buffer[1024];
	/* Verify the written data. */
	ck_assert_ptr_ne(f = fopen(filename, "rb"), NULL);
	ck_assert_uint_eq(fread(buffer, 1, sizeof(buffer), f), 10  * sizeof(uint8_t));
	ck_assert_mem_eq(buffer, pcm_u8, 10 * sizeof(uint8_t));
	fclose(f);

	unlink(filename);
	w->free(w);

} END_TEST

#if ENABLE_MP3LAME
START_TEST(test_writer_mp3) {

	struct writer * w;
	/* For now, the U8 format is not supported in MP3 writer. */
	ck_assert_ptr_eq(w = writer_mp3_new(PCM_FORMAT_U8, 1, 8000,
				32000, 32000, NULL), NULL);
	ck_assert_ptr_ne(w = writer_mp3_new(PCM_FORMAT_S16LE, 1, 8000,
				32000, 32000, NULL), NULL);
	ck_assert_uint_eq(w->type, WRITER_TYPE_MP3);
	ck_assert_uint_eq(w->opened, false);

	w->close(w);
	w->free(w);

} END_TEST
#endif

#if ENABLE_MP3LAME
START_TEST(test_writer_mp3_write) {

	struct writer * w;
	const char * filename = "tc-writer.mp3";
	ck_assert_ptr_ne(w = writer_mp3_new(PCM_FORMAT_S16LE, 1, 8000,
				32000, 32000, "SVAR - test"), NULL);
	ck_assert_int_ne(w->open(w, filename), -1);
	ck_assert_uint_eq(w->opened, true);

	w->write(w, &pcm_s16[0], 5);
	w->write(w, &pcm_s16[5], 5);
	w->close(w);

	unlink(filename);
	w->free(w);

} END_TEST
#endif

#if ENABLE_OPUS
START_TEST(test_writer_opus) {

	struct writer * w;
	/* For now, the U8 format is not supported in OPUS writer. */
	ck_assert_ptr_eq(w = writer_opus_new(PCM_FORMAT_U8, 1, 16000,
				64000, NULL), NULL);
	ck_assert_ptr_ne(w = writer_opus_new(PCM_FORMAT_S16LE, 1, 16000,
				64000, NULL), NULL);
	ck_assert_uint_eq(w->type, WRITER_TYPE_OPUS);
	ck_assert_uint_eq(w->opened, false);

	w->close(w);
	w->free(w);

} END_TEST
#endif

#if ENABLE_OPUS
START_TEST(test_writer_opus_write) {

	struct writer * w;
	const char * filename = "tc-writer.opus";
	ck_assert_ptr_ne(w = writer_opus_new(PCM_FORMAT_S16LE, 1, 16000,
				64000, "SVAR - test"), NULL);
	ck_assert_int_ne(w->open(w, filename), -1);
	ck_assert_uint_eq(w->opened, true);

	w->write(w, &pcm_s16[0], 5);
	w->write(w, &pcm_s16[5], 5);
	w->close(w);

	unlink(filename);
	w->free(w);

} END_TEST
#endif

#if ENABLE_VORBIS
START_TEST(test_writer_vorbis) {

	struct writer * w;
	/* For now, the U8 format is not supported in VORBIS writer. */
	ck_assert_ptr_eq(w = writer_vorbis_new(PCM_FORMAT_U8, 1, 16000,
				32000, 64000, 96000, NULL), NULL);
	ck_assert_ptr_ne(w = writer_vorbis_new(PCM_FORMAT_S16LE, 1, 16000,
				32000, 64000, 96000, NULL), NULL);
	ck_assert_uint_eq(w->type, WRITER_TYPE_VORBIS);
	ck_assert_uint_eq(w->opened, false);

	w->close(w);
	w->free(w);

} END_TEST
#endif

#if ENABLE_VORBIS
START_TEST(test_writer_vorbis_write) {

	struct writer * w;
	const char * filename = "tc-writer.ogg";
	ck_assert_ptr_ne(w = writer_vorbis_new(PCM_FORMAT_S16LE, 1, 16000,
				32000, 64000, 96000, "SVAR - test"), NULL);
	ck_assert_int_ne(w->open(w, filename), -1);
	ck_assert_uint_eq(w->opened, true);

	w->write(w, &pcm_s16[0], 5);
	w->write(w, &pcm_s16[5], 5);
	w->close(w);

	unlink(filename);
	w->free(w);

} END_TEST
#endif

#if ENABLE_SNDFILE
START_TEST(test_writer_wav) {

	struct writer * w;
	ck_assert_ptr_ne(w = writer_wav_new(PCM_FORMAT_U8, 1, 8000), NULL);
	ck_assert_uint_eq(w->type, WRITER_TYPE_WAV);
	ck_assert_uint_eq(w->opened, false);

	w->close(w);
	w->free(w);

} END_TEST
#endif

#if ENABLE_SNDFILE
START_TEST(test_writer_wav_write) {

	struct writer * w;
	const char * filename = "tc-writer.wav";
	ck_assert_ptr_ne(w = writer_wav_new(PCM_FORMAT_S16LE, 1, 8000), NULL);
	ck_assert_int_ne(w->open(w, filename), -1);
	ck_assert_uint_eq(w->opened, true);

	w->write(w, &pcm_s16[0], 5);
	w->write(w, &pcm_s16[5], 5);
	w->close(w);

	FILE * f;
	uint8_t buffer[1024];
	/* Verify the written data. */
	ck_assert_ptr_ne(f = fopen(filename, "rb"), NULL);
	ck_assert_uint_eq(fread(buffer, 1, sizeof(buffer), f), 44 + 10 * sizeof(int16_t));
	ck_assert_mem_eq(buffer + 44, pcm_s16, 10 * sizeof(int16_t));
	fclose(f);

	unlink(filename);
	w->free(w);

} END_TEST
#endif

START_TEST(test_writer_type_to_extension) {
	ck_assert_str_eq(writer_type_to_extension(WRITER_TYPE_RAW), "raw");
#if ENABLE_SNDFILE
	ck_assert_str_eq(writer_type_to_extension(WRITER_TYPE_WAV), "wav");
	ck_assert_str_eq(writer_type_to_extension(WRITER_TYPE_RF64), "wav");
#endif
} END_TEST

START_TEST(test_writer_type_to_string) {
	ck_assert_str_eq(writer_type_to_string(WRITER_TYPE_RAW), "raw");
#if ENABLE_SNDFILE
	ck_assert_str_eq(writer_type_to_string(WRITER_TYPE_WAV), "wav");
	ck_assert_str_eq(writer_type_to_string(WRITER_TYPE_RF64), "rf64");
#endif
} END_TEST

int tcase_init(Suite * s) {

	TCase * tc = tcase_create(__FILE__);
	suite_add_tcase(s, tc);

	tcase_add_test(tc, test_writer_raw);
	tcase_add_test(tc, test_writer_raw_write);

#if ENABLE_MP3LAME
	tcase_add_test(tc, test_writer_mp3);
	tcase_add_test(tc, test_writer_mp3_write);
#endif

#if ENABLE_OPUS
	tcase_add_test(tc, test_writer_opus);
	tcase_add_test(tc, test_writer_opus_write);
#endif

#if ENABLE_VORBIS
	tcase_add_test(tc, test_writer_vorbis);
	tcase_add_test(tc, test_writer_vorbis_write);
#endif

#if ENABLE_SNDFILE
	tcase_add_test(tc, test_writer_wav);
	tcase_add_test(tc, test_writer_wav_write);
#endif

	tcase_add_test(tc, test_writer_type_to_extension);
	tcase_add_test(tc, test_writer_type_to_string);

	return 0;
}
