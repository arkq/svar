/*
 * SVAR - recorder-alsa.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_RECORDER_ALSA_H_
#define SVAR_RECORDER_ALSA_H_

#include "pcm.h"
#include "recorder.h"

struct recorder * recorder_alsa_new(
		enum pcm_format format, unsigned int channels, unsigned int rate);

#endif
