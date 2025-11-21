/*
 * SVAR - tc-rbuf.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include <check.h>
#include <stdio.h>

#include "rbuf.h"

START_TEST(test_rbuf_init) {

	struct rbuf rb;
	ck_assert_int_eq(rbuf_init(&rb, 1024, sizeof(int)), 0);

	ck_assert_ptr_ne(rb.begin, rb.end);
	ck_assert_ptr_eq(rb.begin, rb.head);
	ck_assert_ptr_eq(rb.begin, rb.tail);
	ck_assert_uint_eq(rb.nmemb, 1024);
	ck_assert_uint_eq(rb.size, sizeof(int));
	ck_assert_uint_eq(rb.used, 0);

	rbuf_free(&rb);

} END_TEST

START_TEST(test_rbuf_read_write) {

	struct rbuf rb;
	ck_assert_int_eq(rbuf_init(&rb, 1024, sizeof(int)), 0);

	printf(":...............\n");
	ck_assert_uint_eq(rbuf_read_linear_capacity(&rb), 0);
	ck_assert_uint_eq(rbuf_write_linear_capacity(&rb), 1024);
	ck_assert_uint_eq(rb.used, 0);

	rbuf_write_linear_commit(&rb, 512);
	printf("oooooooo:.......\n");
	ck_assert_uint_eq(rbuf_read_linear_capacity(&rb), 512);
	ck_assert_uint_eq(rbuf_write_linear_capacity(&rb), 512);
	ck_assert_uint_eq(rb.used, 512);

	rbuf_write_linear_commit(&rb, 256);
	printf("oooooooooooo:...\n");
	ck_assert_uint_eq(rbuf_read_linear_capacity(&rb), 768);
	ck_assert_uint_eq(rbuf_write_linear_capacity(&rb), 256);
	ck_assert_uint_eq(rb.used, 768);

	rbuf_read_linear_commit(&rb, 512);
	printf("........oooo:...\n");
	ck_assert_uint_eq(rbuf_read_linear_capacity(&rb), 256);
	ck_assert_uint_eq(rbuf_write_linear_capacity(&rb), 256);
	ck_assert_uint_eq(rb.used, 256);

	rbuf_read_linear_commit(&rb, 256);
	printf("............:...\n");
	ck_assert_uint_eq(rbuf_read_linear_capacity(&rb), 0);
	ck_assert_uint_eq(rbuf_write_linear_capacity(&rb), 256);
	ck_assert_uint_eq(rb.used, 0);

	rbuf_write_linear_commit(&rb, 256);
	printf(":...........oooo\n");
	ck_assert_uint_eq(rbuf_read_linear_capacity(&rb), 256);
	ck_assert_uint_eq(rbuf_write_linear_capacity(&rb), 768);
	ck_assert_uint_eq(rb.used, 256);

	rbuf_read_linear_commit(&rb, 256);
	printf(":...............\n");
	ck_assert_uint_eq(rbuf_read_linear_capacity(&rb), 0);
	ck_assert_uint_eq(rbuf_write_linear_capacity(&rb), 1024);
	ck_assert_uint_eq(rb.used, 0);

	rbuf_free(&rb);

} END_TEST

int tcase_init(Suite * s) {

	TCase * tc = tcase_create(__FILE__);
	suite_add_tcase(s, tc);

	tcase_add_test(tc, test_rbuf_init);
	tcase_add_test(tc, test_rbuf_read_write);

	return 0;
}
