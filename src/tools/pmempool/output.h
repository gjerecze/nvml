/*
 * Copyright (c) 2014-2015, Intel Corporation
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
 * output.h -- declarations of output printing related functions
 */

#include <uuid/uuid.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>

void out_set_vlevel(int vlevel);
void out_set_stream(FILE *stream);
void out_set_prefix(const char *prefix);
void out_set_col_width(unsigned int col_width);
void out_err(const char *fmt, ...);
void outv(int vlevel, const char *fmt, ...);
int outv_check(int vlevel);
void outv_field(int vlevel, const char *field, const char *fmt, ...);
void outv_hexdump(int vlevel, const void *addr, size_t len, size_t offset,
		int sep);
const char *out_get_uuid_str(uuid_t uuid);
const char *out_get_time_str(time_t time);
const char *out_get_size_str(uint64_t size, int human);
const char *out_get_percentage(double percentage);
const char *out_get_checksum(void *addr, size_t len, uint64_t *csump);
const char *out_get_btt_map_entry(uint32_t map);
const char *out_get_pool_type_str(pmem_pool_type_t type);
