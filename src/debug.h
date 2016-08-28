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

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdio.h>


#ifdef DEBUG
#define debug(M, ...) fprintf(stderr, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define debug(M, ...)
#endif

#endif
