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
 * arena.h -- internal definitions for arena
 */

struct arena {
	int associated_threads; /* number of threads using this arena */
	pthread_mutex_t *lock;
	int id; /* index in the pool->arenas array */
	struct pmalloc_pool *pool;
	struct arena_backend_operations *a_ops;
	struct bucket *buckets[MAX_BUCKETS];
};

enum guard_type {
	GUARD_TYPE_UNKNOWN,
	GUARD_TYPE_MALLOC,
	GUARD_TYPE_REALLOC,
	GUARD_TYPE_FREE,

	MAX_GUARD_TYPE
};

struct arena *arena_new(struct pmalloc_pool *p, int arena_id);
void arena_delete(struct arena *a);
bool arena_guard_up(struct arena *arena, uint64_t *ptr, enum guard_type type);
struct bucket *arena_select_bucket(struct arena *arena, size_t size);
bool arena_guard_down(struct arena *arena, uint64_t *ptr, enum guard_type type);
