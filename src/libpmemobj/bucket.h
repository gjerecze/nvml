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
 * bucket.h -- internal definitions for bucket
 */

#define RUN_NALLOCS(_bs)\
((RUNSIZE / ((_bs) + MBLOCK_HEADER_SIZE)))

enum block_container_type {
	CONTAINER_UNKNOWN,
	CONTAINER_CTREE,
	CONTAINER_LIST,

	MAX_CONTAINER_TYPE
};

struct block_container {
	enum block_container_type type;
	size_t unit_size;
};

struct block_container_ops {
	int (*insert)(struct block_container *c, PMEMobjpool *pop,
		struct memory_block m);
	int (*get_rm_exact)(struct block_container *c, struct memory_block m);
	int (*get_rm_bestfit)(struct block_container *c,
		struct memory_block *m);
	int (*get_exact)(struct block_container *c, struct memory_block m);
	int (*is_empty)(struct block_container *c);
};

#define	CNT_OP(_b, _op, ...)\
(_b)->c_ops->_op((_b)->container, ##__VA_ARGS__)

enum bucket_type {
	BUCKET_UNKNOWN,
	BUCKET_HUGE,
	BUCKET_RUN,

	MAX_BUCKET_TYPE
};

struct bucket {
	enum bucket_type type;

	size_t unit_size;

	uint32_t (*calc_units)(struct bucket *b, size_t size);

	pthread_mutex_t lock;

	struct block_container *container;
	struct block_container_ops *c_ops;
};

struct bucket_huge {
	struct bucket super;
};

struct bucket_run {
	struct bucket super;

	uint64_t bitmap_lastval;
	int bitmap_nval;
	int bitmap_nallocs;
	int unit_max;
};

struct bucket *bucket_new(enum bucket_type type,
	enum block_container_type ctype, size_t unit_size, int unit_max);
void bucket_delete(struct bucket *b);
