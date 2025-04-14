/*
 * SVAR - rbuf.h
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef SVAR_RBUF_H_
#define SVAR_RBUF_H_

#include <stddef.h>
#include <stdint.h>

struct rbuf {
	/* read pointer */
	void * head;
	/* write pointer */
	void * tail;
	/* buffer memory block */
	void * begin;
	void * end;
	/* number of used elements */
	size_t used;
	/* number of elements */
	size_t nmemb;
	/* element size */
	size_t size;
};

int rbuf_init(struct rbuf * rb, size_t nmemb, size_t size);
void rbuf_free(struct rbuf * rb);

size_t rbuf_read_linear_capacity(const struct rbuf * rb);
void rbuf_read_linear_commit(struct rbuf * rb, size_t nmemb);
size_t rbuf_write_linear_capacity(const struct rbuf * rb);
void rbuf_write_linear_commit(struct rbuf * rb, size_t nmemb);

#endif
