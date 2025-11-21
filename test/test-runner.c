/*
 * SVAR - test-runner.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include <check.h>

extern int tcase_init(Suite * s);

int main(void) {

	Suite * s = suite_create(__FILE__);
	SRunner * sr = srunner_create(s);

	int rv;
	if ((rv = tcase_init(s)) != 0)
		goto fail;

	srunner_run_all(sr, CK_ENV);
	rv = srunner_ntests_failed(sr) == 0 ? 0 : 1;

fail:
	srunner_free(sr);
	return rv;
}
