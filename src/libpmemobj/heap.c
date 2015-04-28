/*
 * Copyright (c) 2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * heap.c -- heap implementation
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>

#include "libpmem.h"
#include "libpmemobj.h"
#include "util.h"
#include "redo.h"
#include "heap_layout.h"
#include "bucket.h"
#include "lane.h"
#include "out.h"
#include "obj.h"

#define	DEFAULT_BUCKET 0
#define	MAX_BUCKETS 1

struct pmalloc_heap {
	struct heap_layout *layout;
	struct bucket *buckets[MAX_BUCKETS];
	int max_zone;
	int zones_exhausted;
};

static struct heap_layout *
heap_get_layout(PMEMobjpool *pop)
{
	return (void *)pop + pop->heap_offset;
}

static int
heap_max_zone(size_t size)
{
	int max_zone = 0;
	size -= sizeof (struct heap_header);

	while (size > ZONE_MIN_SIZE) {
		max_zone++;
		size -= size < ZONE_MAX_SIZE ? size : ZONE_MAX_SIZE;
	}

	return max_zone;
}

/*
 * get_zone_size_idx -- (internal) calculate zone size index
 */
static uint32_t
get_zone_size_idx(uint32_t zone_id, int max_zone, size_t heap_size)
{
	if (zone_id < max_zone - 1)
		return MAX_CHUNK;

	size_t zone_raw_size = heap_size - zone_id * ZONE_MAX_SIZE;

	zone_raw_size -= sizeof (struct zone);

	return zone_raw_size / CHUNKSIZE;
}

static void
heap_chunk_init(PMEMobjpool *pop, struct chunk_header *hdr, uint32_t size_idx)
{
	struct chunk_header nhdr = {
		.type = CHUNK_TYPE_FREE,
		.flags = 0,
		.size_idx = size_idx
	};
	*hdr = nhdr;
	pop->persist(hdr, sizeof (*hdr));
}

static void
heap_zone_init(PMEMobjpool *pop, uint32_t zone_id)
{
	struct zone *z = &pop->heap->layout->zones[zone_id];
	uint32_t size_idx = get_zone_size_idx(zone_id, pop->heap->max_zone,
			pop->heap_size);

	heap_chunk_init(pop, &z->chunk_headers[0], size_idx);

	struct zone_header nhdr = {
		.size_idx = size_idx,
		.magic = ZONE_HEADER_MAGIC
	};
	z->header = nhdr;
	pop->persist(&z->header.magic, sizeof (z->header.magic));
}

static void
heap_populate_buckets(PMEMobjpool *pop)
{
	struct pmalloc_heap *h = pop->heap;

	uint32_t zone_id = h->zones_exhausted++;
	struct zone *z = &h->layout->zones[zone_id];

	if (z->header.magic != ZONE_HEADER_MAGIC)
		heap_zone_init(pop, zone_id);

	struct bucket *def_bucket = h->buckets[DEFAULT_BUCKET];

	for (uint32_t i = 0; i < z->header.size_idx; ) {
		struct chunk_header *hdr = &z->chunk_headers[i];

		if (hdr->type == CHUNK_TYPE_RUN) {
			/* XXX */
		} else if (hdr->type == CHUNK_TYPE_FREE) {
			bucket_insert_block(def_bucket, i, zone_id,
				hdr->size_idx, 0);
		}

		i += hdr->size_idx;
	}
}

static int
heap_buckets_init(PMEMobjpool *pop)
{
	struct pmalloc_heap *h = pop->heap;

	if ((h->buckets[DEFAULT_BUCKET] = bucket_new(CHUNKSIZE, -1)) == NULL)
		goto error_bucket_new;

	heap_populate_buckets(pop);

	return 0;

error_bucket_new:
	return ENOMEM;
}

struct bucket *
heap_get_best_bucket(PMEMobjpool *pop, size_t size)
{
	return pop->heap->buckets[DEFAULT_BUCKET];
}

void heap_resize_chunk(PMEMobjpool *pop,
	uint32_t chunk_id, uint32_t zone_id, uint32_t new_size_idx)
{
	uint32_t new_chunk_id = chunk_id + new_size_idx;

	struct zone *z = &pop->heap->layout->zones[zone_id];
	struct chunk_header *old_hdr = &z->chunk_headers[chunk_id];
	struct chunk_header *new_hdr = &z->chunk_headers[new_chunk_id];

	uint32_t rem_size_idx = old_hdr->size_idx - new_size_idx;
	heap_chunk_init(pop, new_hdr, rem_size_idx);
	heap_chunk_init(pop, old_hdr, new_size_idx);

	struct bucket *def_bucket = pop->heap->buckets[DEFAULT_BUCKET];
	bucket_insert_block(def_bucket, new_chunk_id, zone_id, rem_size_idx, 0);
}

struct chunk_header *heap_get_chunk_header(PMEMobjpool *pop,
	uint32_t chunk_id, uint32_t zone_id)
{
	return &pop->heap->layout->zones[zone_id].chunk_headers[chunk_id];
}

void *heap_get_chunk_data(PMEMobjpool *pop,
	uint32_t chunk_id, uint32_t zone_id)
{
	return &pop->heap->layout->zones[zone_id].chunks[chunk_id].data;
}

/*
 * heap_boot -- opens the heap region of the pmemobj pool
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
heap_boot(PMEMobjpool *pop)
{
	struct pmalloc_heap *h = Malloc(sizeof (*h));
	int err;
	if (h == NULL) {
		err = ENOMEM;
		goto error_heap_malloc;
	}

	h->max_zone = heap_max_zone(pop->heap_size);
	h->zones_exhausted = 0;
	h->layout = heap_get_layout(pop);
	pop->heap = h;

	if ((err = heap_buckets_init(pop)) != 0)
		goto error_buckets_init;

	return 0;

error_buckets_init:
	Free(h);
	pop->heap = NULL;
error_heap_malloc:
	return err;
}

static void
heap_write_header(struct heap_header *hdr, size_t size)
{
	struct heap_header newhdr = {
		.signature = HEAP_SIGNATURE,
		.major = HEAP_MAJOR,
		.minor = HEAP_MINOR,
		.size = size,
		.chunksize = CHUNKSIZE,
		.chunks_per_zone = MAX_CHUNK,
		.reserved = {0},
		.checksum = 0
	};

	util_checksum(&newhdr, sizeof (newhdr), &newhdr.checksum, 1);
	*hdr = newhdr;
}

/*
 * heap_init -- initializes the heap
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
heap_init(PMEMobjpool *pop)
{
	if (pop->heap_size < HEAP_MIN_SIZE)
		return EINVAL;

	struct heap_layout *layout = heap_get_layout(pop);
	heap_write_header(&layout->header, pop->heap_size);
	pop->persist(&layout->header, sizeof (struct heap_header));

	return 0;
}

/*
 * heap_boot -- cleanups the volatile heap state
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
heap_cleanup(PMEMobjpool *pop)
{
	for (int i = 0; i < MAX_BUCKETS; ++i)
		bucket_delete(pop->heap->buckets[i]);

	Free(pop->heap);

	return 0;
}

static int
heap_verify_header(struct heap_header *hdr)
{
	if (util_checksum(hdr, sizeof (*hdr), &hdr->checksum, 0) != 1)
		return 1;

	if (memcmp(hdr->signature, HEAP_SIGNATURE, HEAP_SIGNATURE_LEN) != 0)
		return 1;

	return 0;
}

static int
heap_verify_zone_header(struct zone_header *hdr)
{
	if (hdr->size_idx == 0)
		return 1;

	return 0;
}

static int
heap_verify_chunk_header(struct chunk_header *hdr)
{
	if (hdr->type == CHUNK_TYPE_UNKNOWN)
		return 1;

	if (hdr->type >= MAX_CHUNK_TYPE)
		return 1;

	if (hdr->flags | ~(CHUNK_FLAG_ZEROED))
		return 1;

	return 0;
}

static int
heap_verify_zone(struct zone *zone)
{
	if (zone->header.magic != ZONE_HEADER_MAGIC)
		return 0;

	if (heap_verify_zone_header(&zone->header))
		return 1;

	uint32_t i;
	for (i = 0; i < zone->header.size_idx; ) {
		if (heap_verify_chunk_header(&zone->chunk_headers[i]))
			return 1;

		i += zone->chunk_headers[i].size_idx;
	}

	if (i != zone->header.size_idx)
		return 1;

	return 0;
}

/*
 * heap_check -- verifies if the heap is consistent and can be opened properly
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
heap_check(PMEMobjpool *pop)
{
	if (pop->heap_size < HEAP_MIN_SIZE)
		return EINVAL;

	struct heap_layout *layout = heap_get_layout(pop);

	int ok = 0;
	ok &= heap_verify_header(&layout->header);
	for (int i = 0; i < heap_max_zone(layout->header.size); ++i) {
		ok &= heap_verify_zone(&layout->zones[i]);
	}

	return ok;
}
