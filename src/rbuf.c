/*
 * SVAR - rbuf.c
 * SPDX-FileCopyrightText: 2025 Arkadiusz Bokowy and contributors
 * SPDX-License-Identifier: MIT
 */

#include "rbuf.h"

#include <stdint.h>
#include <stdlib.h>

int rbuf_init(struct rbuf * rb, size_t nmemb, size_t size) {
	const size_t bytes = nmemb * size;
	if ((rb->begin = malloc(bytes)) == NULL)
		return -1;
	rb->head = rb->begin;
	rb->tail = rb->begin;
	rb->end = (uint8_t *)rb->begin + bytes;
	rb->nmemb = nmemb;
	rb->size = size;
	rb->used = 0;
	return 0;
}

void rbuf_free(struct rbuf * rb) {
	free(rb->begin);
}

/**
 * Number of elements available for linear read. */
size_t rbuf_read_linear_capacity(const struct rbuf * rb) {
	if (rb->head < rb->tail)
		return ((uint8_t *)rb->tail - (uint8_t *)rb->head) / rb->size;
	/* Get the number of elements from the tail to the end of the buffer. */
	const size_t nmemb = ((uint8_t *)rb->end - (uint8_t *)rb->head) / rb->size;
	return rb->used == 0 ? 0 : nmemb;
}

/**
 * Commit elements as already read.
 *
 * The number of elements to commit must be less than or equal to the
 * number of elements available for linear read. */
void rbuf_read_linear_commit(struct rbuf * rb, size_t nmemb) {
	rb->head = (uint8_t *)rb->head + nmemb * rb->size;
	if (rb->head == rb->end)
		rb->head = rb->begin;
	rb->used -= nmemb;
}

/**
 * Number of elements available for linear write. */
size_t rbuf_write_linear_capacity(const struct rbuf * rb) {
	if (rb->tail < rb->head)
		return ((uint8_t *)rb->head - (uint8_t *)rb->tail) / rb->size;
	/* Get the number of elements from the tail to the end of the buffer. */
	const size_t nmemb = ((uint8_t *)rb->end - (uint8_t *)rb->tail) / rb->size;
	return rb->used == rb->nmemb ? 0 : nmemb;
}

/**
 * Commit elements as ready for reading.
 *
 * The number of elements to commit must be less than or equal to the
 * number of elements available for linear write. */
void rbuf_write_linear_commit(struct rbuf * rb, size_t nmemb) {
	rb->tail = (uint8_t *)rb->tail + nmemb * rb->size;
	if (rb->tail == rb->end)
		rb->tail = rb->begin;
	rb->used += nmemb;
}
