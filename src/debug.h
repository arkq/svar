/*
 * SVAR - debug.h
 * Copyright (c) 2014-2017 Arkadiusz Bokowy
 *
 * This file is a part of SVAR.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#ifndef SVAR_DEBUG_H_
#define SVAR_DEBUG_H_

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>

#define error(M, ARGS...) fprintf(stderr, "error: " M "\n", ## ARGS)
#define warn(M, ARGS...) fprintf(stderr, "warn: " M "\n", ## ARGS)
#define info(M, ARGS...) fprintf(stderr, "info: " M "\n", ## ARGS)

#if DEBUG
# define debug(M, ARGS...) fprintf(stderr, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ## ARGS)
#else
# define debug(M, ARGS...) do {} while (0)
#endif

#endif
