/*
 * Copyright 2016-2018, Intel Corporation
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
 *     * Neither the name of the copyright holder nor the names of its
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
 * memops.h -- aggregated memory operations helper definitions
 */

#ifndef LIBPMEMOBJ_MEMOPS_H
#define LIBPMEMOBJ_MEMOPS_H 1

#include <stddef.h>
#include <stdint.h>

#include "vec.h"
#include "pmemops.h"
#include "redo.h"
#include "lane.h"

enum operation_log_type {
	LOG_PERSISTENT,
	LOG_TRANSIENT,

	MAX_OPERATION_LOG_TYPE
};

struct operation_log {
	size_t capacity;
	size_t size;
	struct redo_log *redo;
};

/*
 * operation_context -- context of an ongoing palloc operation
 */
struct operation_context {
	void *base;
	redo_extend_fn extend;

	const struct redo_ctx *redo_ctx;
	const struct pmem_ops *p_ops;

	struct redo_log *redo;
	size_t redo_base_capacity;
	size_t redo_capacity;
	struct redo_next next;

	int in_progress;

	struct operation_log logs[MAX_OPERATION_LOG_TYPE];
};

struct operation_context *operation_new(void *base,
	const struct redo_ctx *redo_ctx,
	struct redo_log *redo, size_t redo_base_capacity,
	redo_extend_fn extend);

void operation_init(struct operation_context *ctx);
void operation_start(struct operation_context *ctx);

void operation_delete(struct operation_context *ctx);

int operation_add_entry(struct operation_context *ctx,
	void *ptr, uint64_t value, enum redo_operation_type type);
int operation_add_typed_entry(struct operation_context *ctx,
	void *ptr, uint64_t value,
	enum redo_operation_type type, enum operation_log_type log_type);
int operation_reserve(struct operation_context *ctx, size_t new_capacity);
void operation_process(struct operation_context *ctx);
void operation_cancel(struct operation_context *ctx);

#endif
