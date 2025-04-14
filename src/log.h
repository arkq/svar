/*
 * SVAR - log.h
 * SPDX-FileCopyrightText: 2014-2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef SVAR_LOG_H_
#define SVAR_LOG_H_

#include <stdio.h>

#define error(M, ARGS...) fprintf(stderr, "error: " M "\n", ## ARGS)
#define warn(M, ARGS...) fprintf(stderr, "warn: " M "\n", ## ARGS)
#define info(M, ARGS...) fprintf(stderr, "info: " M "\n", ## ARGS)
#define debug(M, ARGS...) fprintf(stderr, "debug: " M "\n", ## ARGS)

#endif
