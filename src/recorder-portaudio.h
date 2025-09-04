/*
 * SVAR - recorder-portaudio.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_RECORDER_PORTAUDIO_H_
#define SVAR_RECORDER_PORTAUDIO_H_

#include "pcm.h"
#include "recorder.h"

struct recorder * recorder_pa_new(
		enum pcm_format format, unsigned int channels, unsigned int rate);

void recorder_pa_list_devices(void);

#endif
