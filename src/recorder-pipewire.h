/*
 * SVAR - recorder-pipewire.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_RECORDER_PIPEWIRE_H_
#define SVAR_RECORDER_PIPEWIRE_H_

#include "pcm.h"
#include "recorder.h"

struct recorder * recorder_pw_new(
		enum pcm_format format, unsigned int channels, unsigned int rate);

#endif
