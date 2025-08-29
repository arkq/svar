/*
 * SVAR - test-runner.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include <check.h>

extern void tcase_init(Suite * s);

int main(void) {

	Suite * s = suite_create(__FILE__);
	SRunner * sr = srunner_create(s);

	tcase_init(s);

	srunner_run_all(sr, CK_ENV);
	int nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return nf == 0 ? 0 : 1;
}
