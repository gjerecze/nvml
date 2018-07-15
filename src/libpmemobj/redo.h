/*
 * Copyright 2015-2018, Intel Corporation
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
 * redo.h -- redo log public interface
 */

#ifndef LIBPMEMOBJ_REDO_H
#define LIBPMEMOBJ_REDO_H 1

#include <stddef.h>
#include <stdint.h>

#include "vec.h"
#include "pmemops.h"

struct redo_log_entry_base {
	uint64_t offset;
};

/*
 * redo_log_entry_val -- redo log entry
 */
struct redo_log_entry_val {
	struct redo_log_entry_base base;	/* offset with operation type flag */
	uint64_t value;
};

/*
 * redo_log_entry_buf - redo log buffer entry
 */
struct redo_log_entry_buf {
	struct redo_log_entry_base base; /* offset with operation type flag */
	uint64_t size;		/* size of the buffer to be modified */
	uint8_t data[];		/* content to fill in */
};

#define REDO_LOG(capacity_bytes) {\
	/* 128 bytes of metadata (two cachelines) */\
	uint64_t checksum; /* checksum of redo header and its entries */\
	uint64_t nentries; /* total number of entries (incl. next redo) */\
	uint64_t next; /* offset of redo log extension */\
	uint64_t capacity; /* capacity of this redo in bytes */\
	uint64_t unused[4]; /* must be 0 */\
	/* N bytes of data */\
	uint8_t data[];\
}\

#define SIZEOF_REDO_LOG(base_capacity)\
(sizeof(struct redo_log) + redo_base_bytes)

struct redo_log REDO_LOG(0);

VEC(redo_next, uint64_t);

enum redo_operation_type {
	REDO_OPERATION_SET	=	0b000,
	REDO_OPERATION_AND	=	0b001,
	REDO_OPERATION_OR	=	0b010,
	REDO_OPERATION_BUF_SET	=	0b101,
	REDO_OPERATION_BUF_CPY	=	0b110,
};

typedef int (*redo_check_offset_fn)(void *ctx, uint64_t offset);
typedef int (*redo_extend_fn)(void *, uint64_t *);

size_t redo_log_capacity(struct redo_log *redo, size_t redo_base_bytes,
	void *base);
void redo_log_rebuild_next_vec(struct redo_log *redo, struct redo_next *next,
	void *base);

int redo_log_reserve(struct redo_log *redo,
	size_t redo_base_bytes, size_t *new_capacity_bytes,
	redo_extend_fn extend, struct redo_next *next, void *base);
void redo_log_store(struct redo_log *dest,
	struct redo_log *src, size_t nentries, size_t redo_base_bytes,
	struct redo_next *next, struct pmem_ops *p_ops);
void redo_log_clobber(struct redo_log *dest, struct redo_next *next,
	const struct pmem_ops *p_ops);
void redo_log_process(struct redo_log *redo, const struct pmem_ops *p_ops);

size_t redo_log_nentries(struct redo_log *redo);

uint64_t redo_log_entry_offset(const struct redo_log_entry_base *entry);
enum redo_operation_type redo_log_entry_type(
	const struct redo_log_entry_base *entry);

void redo_log_entry_val_create(struct redo_log_entry_base *entry, uint64_t *ptr,
	uint64_t value, enum redo_operation_type type, const void *base);

void redo_log_entry_buf_create(struct redo_log_entry_base *entry, uint64_t *ptr,
	uint64_t size, const void *src, enum redo_operation_type type,
	const struct pmem_ops *p_ops);

void redo_log_entry_apply(const struct redo_log_entry_base *e,
	const struct pmem_ops *p_ops);

void redo_log_recover(struct redo_log *redo, const struct pmem_ops *p_ops);
int redo_log_check(struct redo_log *redo, const struct pmem_ops *p_ops);

#endif
