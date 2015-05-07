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
 * obj_heap.c -- unit test for bucket
 */
#include "libpmem.h"
#include "libpmemobj.h"
#include "util.h"
#include "redo.h"
#include "heap_layout.h"
#include "bucket.h"
#include "lane.h"
#include "obj.h"
#include "heap.h"
#include "pmalloc.h"
#include "unittest.h"

#define	MOCK_BUCKET ((void *)0xABC)

FUNC_MOCK_RET_ALWAYS(bucket_new, struct bucket *, MOCK_BUCKET);
FUNC_MOCK_RET_ALWAYS(bucket_delete, void *, NULL);
FUNC_MOCK_RET_ALWAYS(bucket_insert_block, int, 0);

#define	MOCK_POOL_SIZE PMEMOBJ_MIN_POOL

#define	CHUNK_FIRST	1
#define	CHUNK_SECOND	2
#define	CHUNK_FIRST_NEW_SIZE_IDX 1

struct mock_pop {
	PMEMobjpool p;
	void *heap;
};

void
test_heap()
{
	struct mock_pop *pop = Malloc(MOCK_POOL_SIZE);
	memset(pop, 0, MOCK_POOL_SIZE);
	pop->p.heap_size = MOCK_POOL_SIZE - sizeof (PMEMobjpool);
	pop->p.heap_offset = (uint64_t)((uint64_t)&pop->heap - (uint64_t)pop);
	pop->p.persist = (persist_fn)pmem_msync;

	ASSERT(heap_check(&pop->p) != 0);
	ASSERT(heap_init(&pop->p) == 0);
	ASSERT(heap_boot(&pop->p) == 0);
	ASSERT(pop->p.heap != NULL);

	ASSERT(heap_get_best_bucket(&pop->p, 1) == MOCK_BUCKET);

	struct chunk_header *hdr;
	ASSERT((hdr = heap_get_chunk_header(&pop->p, CHUNK_FIRST, 1)) != NULL);
	uint32_t csize = hdr->size_idx;

	heap_resize_chunk(&pop->p, CHUNK_FIRST, 1, CHUNK_FIRST_NEW_SIZE_IDX);
	ASSERT(heap_check(&pop->p) == 0);
	ASSERT((hdr = heap_get_chunk_header(&pop->p, CHUNK_FIRST, 1)) != NULL);
	ASSERT(hdr->size_idx == CHUNK_FIRST_NEW_SIZE_IDX);
	ASSERT((hdr = heap_get_chunk_header(&pop->p, CHUNK_SECOND, 1)) != NULL);
	ASSERT(hdr->size_idx == (csize - CHUNK_FIRST_NEW_SIZE_IDX));

	ASSERT(heap_get_chunk_data(&pop->p, CHUNK_FIRST, 1) != NULL);
	ASSERT(heap_get_chunk_data(&pop->p, CHUNK_SECOND, 1) != NULL);

	ASSERT(heap_check(&pop->p) == 0);
	ASSERT(heap_cleanup(&pop->p) == 0);
	ASSERT(pop->p.heap == NULL);

	Free(pop);
}

int
main(int argc, char *argv[])
{
	START(argc, argv, "obj_heap");

	test_heap();

	DONE(NULL);
}
