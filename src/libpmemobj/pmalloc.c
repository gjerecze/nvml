/*
 * Copyright (c) 2015-2016, Intel Corporation
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
 * pmalloc.c -- persistent malloc implementation
 */

#include <errno.h>

#include "libpmemobj.h"
#include "util.h"
#include "redo.h"
#include "pmalloc.h"
#include "lane.h"
#include "list.h"
#include "obj.h"
#include "out.h"
#include "heap.h"
#include "bucket.h"
#include "heap_layout.h"
#include "valgrind_internal.h"

enum alloc_op_redo {
	ALLOC_OP_REDO_PTR_OFFSET,
	ALLOC_OP_REDO_HEADER,

	MAX_ALLOC_OP_REDO
};

#define USABLE_SIZE(_a)\
((_a)->size - sizeof (struct allocation_header))

/*
 * alloc_write_header -- (internal) creates allocation header
 */
static void
alloc_write_header(PMEMobjpool *pop, struct allocation_header *alloc,
	uint32_t chunk_id, uint32_t zone_id, uint64_t size)
{
	VALGRIND_ADD_TO_TX(alloc, sizeof (*alloc));
	alloc->chunk_id = chunk_id;
	alloc->size = size;
	alloc->zone_id = zone_id;
	VALGRIND_REMOVE_FROM_TX(alloc, sizeof (*alloc));
	pop->persist(pop, alloc, sizeof (*alloc));
}

/*
 * alloc_get_header -- (internal) calculates the address of allocation header
 */
static struct allocation_header *
alloc_get_header(PMEMobjpool *pop, uint64_t off)
{
	void *ptr = (char *)pop + off;
	struct allocation_header *alloc = (void *)((char *)ptr -
			sizeof (*alloc));

	return alloc;
}

/*
 * pop_offset -- (internal) calculates offset of ptr in the pool
 */
static uint64_t
pop_offset(PMEMobjpool *pop, void *ptr)
{
	return (uint64_t)ptr - (uint64_t)pop;
}

/*
 * calc_block_offset -- (internal) calculates the block offset of allocation
 */
static uint16_t
calc_block_offset(PMEMobjpool *pop, struct allocation_header *alloc,
	size_t unit_size)
{
	uint16_t block_off = 0;
	if (unit_size != CHUNKSIZE) {
		struct memory_block m = {alloc->chunk_id, alloc->zone_id, 0, 0};
		void *data = heap_get_block_data(pop, m);
		uintptr_t diff = (uintptr_t)alloc - (uintptr_t)data;
		ASSERT(diff <= RUNSIZE);
		ASSERT((size_t)diff / unit_size <= UINT16_MAX);
		ASSERT(diff % unit_size == 0);
		block_off = (uint16_t)((size_t)diff / unit_size);
	}

	return block_off;
}

/*
 * get_mblock_from_alloc -- (internal) returns allocation memory block
 */
static struct memory_block
get_mblock_from_alloc(PMEMobjpool *pop, struct allocation_header *alloc)
{
	struct memory_block mblock = {
		alloc->chunk_id,
		alloc->zone_id,
		0,
		0
	};

	uint64_t unit_size = heap_get_chunk_block_size(pop, mblock);
	mblock.block_off = calc_block_offset(pop, alloc, unit_size);
	mblock.size_idx = CALC_SIZE_IDX(unit_size, alloc->size);

	return mblock;
}

/*
 * persist_alloc -- (internal) performs a persistent allocation of the
 *	memory block previously reserved by volatile bucket
 */
static void
persist_alloc(PMEMobjpool *pop,
	struct memory_block m, uint64_t real_size, uint64_t *off,
	void (*constructor)(PMEMobjpool *pop, void *ptr, size_t usable_size,
	void *arg), void *arg, uint64_t data_off, int mod_undo, struct redo_log *redo, size_t nentries)
{
#ifdef DEBUG
	if (heap_block_is_allocated(pop, m)) {
		ERR("heap corruption");
		ASSERT(0);
	}
#endif /* DEBUG */

	uint64_t op_result = 0;

	void *block_data = heap_get_block_data(pop, m);
	void *datap = (char *)block_data + sizeof (struct allocation_header);
	void *userdatap = (char *)datap + data_off;

	ASSERT((uint64_t)block_data % _POBJ_CL_ALIGNMENT == 0);

	/* mark everything (including headers) as accessible */
	VALGRIND_DO_MAKE_MEM_UNDEFINED(pop, block_data, real_size);
	/* mark space as allocated */
	VALGRIND_DO_MEMPOOL_ALLOC(pop, userdatap,
			real_size -
			sizeof (struct allocation_header) - data_off);

	alloc_write_header(pop, block_data, m.chunk_id, m.zone_id, real_size);

	if (constructor != NULL)
		constructor(pop, userdatap,
			real_size - sizeof (struct allocation_header) -
			data_off, arg);

	heap_lock_if_run(pop, m);

	uint64_t *hdr = heap_get_block_header(pop, m, HEAP_OP_ALLOC, &op_result);

	uint64_t off_value = pop_offset(pop, datap) + (mod_undo ? data_off : 0);
	if (OBJ_PTR_IS_VALID(pop, off)) {
		struct lane_section *lane;
		lane_hold(pop, &lane, LANE_SECTION_ALLOCATOR);

		struct allocator_lane_section *sec =
			(struct allocator_lane_section *)lane->layout;

		int idx = 0;

		redo_log_store(pop, sec->redo, idx++,
			pop_offset(pop, off), off_value);

		redo_log_store(pop, sec->redo, idx++,
			pop_offset(pop, hdr), op_result);
		for (size_t i = 0; i < nentries; ++i)
			redo_log_store(pop, sec->redo, idx++,
				redo[i].offset, redo[i].value);

		redo_log_set_last(pop, sec->redo, idx - 1);

		redo_log_process(pop, sec->redo, idx);

		lane_release(pop);
	} else {
		*hdr = op_result;
		pop->persist(pop, hdr, sizeof (*hdr));
		if (off != NULL) {
			*off = off_value;
		}
	}

	heap_unlock_if_run(pop, m);
}

/*
 * pmalloc -- allocates a new block of memory
 *
 * The pool offset is written persistently into the off variable.
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
pmalloc(PMEMobjpool *pop, uint64_t *off, size_t size, uint64_t data_off)
{
	return pmalloc_construct(pop, off, size, NULL, NULL, data_off);
}

/*
 * pmalloc_construct -- allocates a new block of memory with a constructor
 *
 * The block offset is written persistently into the off variable, but only
 * after the constructor function has been called.
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
pmalloc_construct(PMEMobjpool *pop, uint64_t *off, size_t size,
	void (*constructor)(PMEMobjpool *pop, void *ptr,
	size_t usable_size, void *arg), void *arg, uint64_t data_off)
{
	return pmalloc_construct_redo(pop, off, size, constructor, arg, data_off, 0, NULL, 0);
}

int
pmalloc_construct_redo(PMEMobjpool *pop, uint64_t *off, size_t size,
	void (*constructor)(PMEMobjpool *pop, void *ptr, size_t usable_size,
	void *arg), void *arg, uint64_t data_off, int mod_undo, struct redo_log *redo, size_t nentries)
{
	int err;

	size_t sizeh = size + sizeof (struct allocation_header);

	struct bucket *b = heap_get_best_bucket(pop, sizeh);

	struct memory_block m = {0, 0, 0, 0};

	m.size_idx = b->calc_units(b, sizeh);

	err = heap_get_bestfit_block(pop, b, &m);

	if (err == ENOMEM && b->type == BUCKET_HUGE)
		goto out; /* there's only one huge bucket */

	if (err == ENOMEM) {
		/*
		 * There's no more available memory in the common heap and in
		 * this lane cache, fallback to the auxiliary (shared) bucket.
		 */
		b = heap_get_auxiliary_bucket(pop, sizeh);
		err = heap_get_bestfit_block(pop, b, &m);
	}

	if (err == ENOMEM) {
		/*
		 * The auxiliary bucket cannot satisfy our request, borrow
		 * memory from other caches.
		 */
		heap_drain_to_auxiliary(pop, b, m.size_idx);
		err = heap_get_bestfit_block(pop, b, &m);
	}

	if (err == ENOMEM) {
		/* we are completely out of memory */
		goto out;
	}

	/*
	 * Now that the memory is reserved we can go ahead with making the
	 * allocation persistent.
	 */
	uint64_t real_size = b->unit_size * m.size_idx;
	persist_alloc(pop, m, real_size, off, constructor, arg, data_off, mod_undo, redo, nentries);
	err = 0;
out:

	return err;
}

int
base_free_op(PMEMobjpool *pop, struct bucket *b, uint64_t *dest_off, uint64_t data_off, struct memory_block m, size_t dest_size)
{
	#ifdef DEBUG
		if (!heap_block_is_allocated(pop, m)) {
			ERR("Double free or heap corruption");
			ASSERT(0);
		}
	#endif /* DEBUG */

	heap_lock_if_run(pop, m);
	uint64_t op_result;
	uint64_t *hdr;
	struct memory_block res = heap_free_block(pop, b, m, &hdr, &op_result);

	if (OBJ_PTR_IS_VALID(pop, dest_off)) {
		struct lane_section *lane;
		lane_hold(pop, &lane, LANE_SECTION_ALLOCATOR);

		struct allocator_lane_section *sec =
			(struct allocator_lane_section *)lane->layout;

		int idx = 0;
		redo_log_store(pop, sec->redo, idx++,
			pop_offset(pop, dest_off), 0);
		redo_log_store(pop, sec->redo, idx++,
			pop_offset(pop, hdr), op_result);

		redo_log_set_last(pop, sec->redo, idx - 1);
		redo_log_process(pop, sec->redo, idx);

		lane_release(pop);
	} else {
		*hdr = op_result;
		pop->persist(pop, hdr, sizeof (*hdr));
		if (dest_off != NULL)
			*dest_off = 0;
	}

	heap_unlock_if_run(pop, m);

	VALGRIND_DO_MEMPOOL_FREE(pop,
			heap_get_block_data(pop, m) +
			sizeof (struct allocation_header) + data_off);

	/* we might have been operating on inactive run */
	if (b != NULL) {
		CNT_OP(b, insert, pop, res);

		if (b->type == BUCKET_RUN)
			heap_degrade_run_if_empty(pop, b, res);
	}

	return 0;
}

int
base_op(PMEMobjpool *pop, uint64_t *dest_off, uint64_t off, size_t size,
	void (*constructor)(PMEMobjpool *pop, void *ptr, size_t usable_size, void *arg),
	void *arg, uint64_t data_off, struct redo_log *redo, size_t nentries)
{
	struct bucket *b = NULL;
	struct allocation_header *alloc = NULL;
	struct memory_block m = {0, 0, 0, 0};

	if (off != 0) {
		alloc = alloc_get_header(pop, off - data_off);
		b = heap_get_chunk_bucket(pop, alloc->chunk_id, alloc->zone_id);
	}

	size_t sizeh = size + sizeof (struct allocation_header);

	if (alloc != NULL) {
		m = get_mblock_from_alloc(pop, alloc);

		if (size == 0)
			return base_free_op(pop, b, dest_off, data_off, m, 0);

		if (size == USABLE_SIZE(alloc))
			return 0;

		if (size < USABLE_SIZE(alloc)) {
			uint64_t unit_size = heap_get_chunk_block_size(pop, m);
			uint32_t shrink_to = CALC_SIZE_IDX(unit_size, sizeh);
			if (shrink_to == m.size_idx)
				return 0; /* noop */

			m.size_idx -= shrink_to;
			m.block_off += shrink_to;
			base_free_op(pop, b, dest_off, data_off, m, sizeh);
		}

		if (size > USABLE_SIZE(alloc)) {
			uint64_t unit_size = heap_get_chunk_block_size(pop, m);
			uint32_t grow_to = CALC_SIZE_IDX(unit_size, sizeh);
		}
	}

	return 0;
}

/*
 * prealloc -- resizes in-place a previously allocated memory block
 *
 * The block offset is written persistently into the off variable.
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
prealloc(PMEMobjpool *pop, uint64_t *off, size_t size, uint64_t data_off)
{
	return prealloc_construct(pop, off, size, NULL, NULL, data_off);
}

int
prealloc_free_alloc(PMEMobjpool *pop, uint64_t *off, size_t size,
	void (*constructor)(PMEMobjpool *pop, void *ptr,
	size_t usable_size, void *arg), void *arg, uint64_t data_off)
{
	if (prealloc_construct(pop, off, size, constructor, arg, data_off) == 0)
		return 0;

	struct allocation_header *alloc = alloc_get_header(pop, *off - data_off);

	struct bucket *b = heap_get_chunk_bucket(pop,
		alloc->chunk_id, alloc->zone_id);

	struct memory_block m = get_mblock_from_alloc(pop, alloc);

#ifdef DEBUG
	if (!heap_block_is_allocated(pop, m)) {
		ERR("Double free or heap corruption");
		ASSERT(0);
	}
#endif /* DEBUG */

	heap_lock_if_run(pop, m);

	uint64_t op_result;
	uint64_t *hdr;
	struct memory_block res = heap_free_block(pop, b, m, &hdr, &op_result);

	if (OBJ_PTR_IS_VALID(pop, off)) {
		struct lane_section *lane;
		lane_hold(pop, &lane, LANE_SECTION_ALLOCATOR);

		struct allocator_lane_section *sec =
			(struct allocator_lane_section *)lane->layout;

		int idx = 0;
		redo_log_store(pop, sec->redo, idx++,
			pop_offset(pop, off), 0);
		redo_log_store(pop, sec->redo, idx++,
			pop_offset(pop, hdr), op_result);

		redo_log_set_last(pop, sec->redo, idx - 1);
		redo_log_process(pop, sec->redo, idx);

		lane_release(pop);
	} else {
		*hdr = op_result;
		pop->persist(pop, hdr, sizeof (*hdr));
		if (off != NULL)
			*off = 0;
	}

	heap_unlock_if_run(pop, m);

	VALGRIND_DO_MEMPOOL_FREE(pop,
			(char *)alloc + sizeof (*alloc) + data_off);

	/* we might have been operating on inactive run */
	if (b != NULL) {
		CNT_OP(b, insert, pop, res);

		if (b->type == BUCKET_RUN)
			heap_degrade_run_if_empty(pop, b, res);
	}

	return 0;
}

/*
 * prealloc_construct -- resizes an existing memory block with a constructor
 *
 * The block offset is written persistently into the off variable, but only
 * after the constructor function has been called.
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
int
prealloc_construct(PMEMobjpool *pop, uint64_t *off, size_t size,
	void (*constructor)(PMEMobjpool *pop, void *ptr,
	size_t usable_size, void *arg), void *arg, uint64_t data_off)
{
	if (size <= pmalloc_usable_size(pop, *off))
		return 0;

	size_t sizeh = size + sizeof (struct allocation_header);

	int err;

	struct allocation_header *alloc = alloc_get_header(pop, *off);

	struct lane_section *lane;
	lane_hold(pop, &lane, LANE_SECTION_ALLOCATOR);

	struct bucket *b = heap_get_best_bucket(pop, alloc->size);

	uint32_t add_size_idx = b->calc_units(b, sizeh - alloc->size);
	uint32_t new_size_idx = b->calc_units(b, sizeh);
	uint64_t real_size = new_size_idx * b->unit_size;

	struct memory_block cnt = get_mblock_from_alloc(pop, alloc);

	heap_lock_if_run(pop, cnt);

	struct memory_block next = {0, 0, 0, 0};
	if ((err = heap_get_adjacent_free_block(pop, b, &next, cnt, 0)) != 0)
		goto out;

	if (next.size_idx < add_size_idx) {
		err = ENOMEM;
		goto out;
	}

	if ((err = heap_get_exact_block(pop, b, &next, add_size_idx)) != 0)
		goto out;

	struct memory_block *blocks[2] = {&cnt, &next};
	uint64_t op_result;
	void *hdr;
	struct memory_block m =
		heap_coalesce(pop, blocks, 2, HEAP_OP_ALLOC, &hdr, &op_result);

	void *block_data = heap_get_block_data(pop, m);
	void *datap = (char *)block_data + sizeof (struct allocation_header);
	void *userdatap = (char *)datap + data_off;

	/* mark new part as accessible and undefined */
	VALGRIND_DO_MAKE_MEM_UNDEFINED(pop, (char *)block_data + alloc->size,
			real_size - alloc->size);
	/* resize allocated space */
	VALGRIND_DO_MEMPOOL_CHANGE(pop, userdatap, userdatap,
		real_size  - sizeof (struct allocation_header) - data_off);

	if (constructor != NULL)
		constructor(pop, userdatap,
			real_size - sizeof (struct allocation_header) -
			data_off, arg);

	struct allocator_lane_section *sec =
		(struct allocator_lane_section *)lane->layout;

	redo_log_store(pop, sec->redo, ALLOC_OP_REDO_PTR_OFFSET,
		pop_offset(pop, &alloc->size), real_size);
	redo_log_store_last(pop, sec->redo, ALLOC_OP_REDO_HEADER,
		pop_offset(pop, hdr), op_result);

	redo_log_process(pop, sec->redo, MAX_ALLOC_OP_REDO);

out:
	heap_unlock_if_run(pop, cnt);
	lane_release(pop);

	return err;
}

/*
 * pmalloc_usable_size -- returns the number of bytes in the memory block
 */
size_t
pmalloc_usable_size(PMEMobjpool *pop, uint64_t off)
{
	return USABLE_SIZE(alloc_get_header(pop, off));
}

void
pfree_redo(PMEMobjpool *pop, uint64_t *off, uint64_t data_off, struct redo_log *redo, size_t nentries)
{
	struct allocation_header *alloc = alloc_get_header(pop, *off - data_off);

	struct bucket *b = heap_get_chunk_bucket(pop,
		alloc->chunk_id, alloc->zone_id);

	struct memory_block m = get_mblock_from_alloc(pop, alloc);

#ifdef DEBUG
	if (!heap_block_is_allocated(pop, m)) {
		ERR("Double free or heap corruption");
		ASSERT(0);
	}
#endif /* DEBUG */

	heap_lock_if_run(pop, m);

	uint64_t op_result;
	uint64_t *hdr;
	struct memory_block res = heap_free_block(pop, b, m, &hdr, &op_result);

	if (OBJ_PTR_IS_VALID(pop, off)) {
		struct lane_section *lane;
		lane_hold(pop, &lane, LANE_SECTION_ALLOCATOR);

		struct allocator_lane_section *sec =
			(struct allocator_lane_section *)lane->layout;

		int idx = 0;
		redo_log_store(pop, sec->redo, idx++,
			pop_offset(pop, off), 0);
		redo_log_store(pop, sec->redo, idx++,
			pop_offset(pop, hdr), op_result);

		for (size_t i = 0; i < nentries; ++i)
			redo_log_store(pop, sec->redo, idx++, redo[i].offset, redo[i].value);

		redo_log_set_last(pop, sec->redo, idx - 1);
		redo_log_process(pop, sec->redo, idx);

		lane_release(pop);
	} else {
		*hdr = op_result;
		pop->persist(pop, hdr, sizeof (*hdr));
		if (off != NULL)
			*off = 0;
	}

	heap_unlock_if_run(pop, m);

	VALGRIND_DO_MEMPOOL_FREE(pop,
			(char *)alloc + sizeof (*alloc) + data_off);

	/* we might have been operating on inactive run */
	if (b != NULL) {
		CNT_OP(b, insert, pop, res);

		if (b->type == BUCKET_RUN)
			heap_degrade_run_if_empty(pop, b, res);
	}
}

/*
 * pfree -- deallocates a memory block previously allocated by pmalloc
 *
 * A zero value is written persistently into the off variable.
 *
 * If successful function returns zero. Otherwise an error number is returned.
 */
void
pfree(PMEMobjpool *pop, uint64_t *off, uint64_t data_off)
{
	pfree_redo(pop, off, data_off, NULL, 0);
}

static int
pmalloc_search_cb(uint64_t off, void *arg)
{
	uint64_t *prev = arg;

	if (*prev == UINT64_MAX) {
		*prev = off;

		return 1;
	}

	if (off == *prev)
		*prev = UINT64_MAX;

	return 0;
}

uint64_t
pmalloc_first(PMEMobjpool *pop)
{
	uint64_t off_search = UINT64_MAX;
	struct memory_block m = {0, 0, 0, 0};

	heap_foreach_object(pop, pmalloc_search_cb, &off_search, m);

	if (off_search == UINT64_MAX)
		return 0;

	return off_search + sizeof (struct allocation_header);
}

uint64_t
pmalloc_next(PMEMobjpool *pop, uint64_t off)
{
	struct allocation_header *alloc = alloc_get_header(pop, off);
	struct memory_block m = get_mblock_from_alloc(pop, alloc);

	uint64_t off_search = off - sizeof (struct allocation_header);

	heap_foreach_object(pop, pmalloc_search_cb, &off_search, m);

	if (off_search == (off - sizeof (struct allocation_header)) || off_search == 0 || off_search == UINT64_MAX)
		return 0;

	return off_search + sizeof (struct allocation_header);
}

/*
 * lane_allocator_construct -- create allocator lane section
 */
static int
lane_allocator_construct(PMEMobjpool *pop, struct lane_section *section)
{
	return 0;
}

/*
 * lane_allocator_destruct -- destroy allocator lane section
 */
static void
lane_allocator_destruct(PMEMobjpool *pop, struct lane_section *section)
{
	/* nop */
}

/*
 * lane_allocator_recovery -- recovery of allocator lane section
 */
static int
lane_allocator_recovery(PMEMobjpool *pop, struct lane_section_layout *section)
{
	struct allocator_lane_section *sec =
		(struct allocator_lane_section *)section;

	redo_log_recover(pop, sec->redo, MAX_ALLOC_OP_REDO);

	return 0;
}

/*
 * lane_allocator_check -- consistency check of allocator lane section
 */
static int
lane_allocator_check(PMEMobjpool *pop, struct lane_section_layout *section)
{
	LOG(3, "allocator lane %p", section);

	struct allocator_lane_section *sec =
		(struct allocator_lane_section *)section;

	int ret;
	if ((ret = redo_log_check(pop, sec->redo, MAX_ALLOC_OP_REDO)) != 0)
		ERR("allocator lane: redo log check failed");

	return ret;
}

/*
 * lane_allocator_init -- initializes allocator section
 */
static int
lane_allocator_boot(PMEMobjpool *pop)
{
	return heap_boot(pop);
}

static struct section_operations allocator_ops = {
	.construct = lane_allocator_construct,
	.destruct = lane_allocator_destruct,
	.recover = lane_allocator_recovery,
	.check = lane_allocator_check,
	.boot = lane_allocator_boot
};

SECTION_PARM(LANE_SECTION_ALLOCATOR, &allocator_ops);
