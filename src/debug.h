/*
 * SVAR - debug.h
 * Copyright (c) 2014-2016 Arkadiusz Bokowy
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

#if DEBUG
# define debug(M, ARGS...) fprintf(stderr, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ## ARGS)
#else
# define debug(M, ARGS...) do {} while (0)
#endif

#endif
