/*
 * Copyright 2014-2017, Intel Corporation
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
 * libpmem.h -- definitions of libpmem entry points
 *
 * This library provides support for programming with persistent memory (pmem).
 *
 * libpmem provides support for using raw pmem directly.
 *
 * See libpmem(3) for details.
 */

#ifndef LIBPMEM_H
#define LIBPMEM_H 1

#include <setjmp.h>

#ifdef _WIN32
#include <pmemcompat.h>

#ifndef NVML_UTF8_API
#define pmem_map_file pmem_map_fileW
#define pmem_check_version pmem_check_versionW
#define pmem_errormsg pmem_errormsgW
#else
#define pmem_map_file pmem_map_fileU
#define pmem_check_version pmem_check_versionU
#define pmem_errormsg pmem_errormsgU
#endif

#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

/*
 * flags supported by pmem_map_file()
 */
#define PMEM_FILE_CREATE	(1 << 0)
#define PMEM_FILE_EXCL		(1 << 1)
#define PMEM_FILE_SPARSE	(1 << 2)
#define PMEM_FILE_TMPFILE	(1 << 3)

#ifndef _WIN32
void *pmem_map_file(const char *path, size_t len, int flags, mode_t mode,
	size_t *mapped_lenp, int *is_pmemp);
#else
void *pmem_map_fileU(const char *path, size_t len, int flags, mode_t mode,
	size_t *mapped_lenp, int *is_pmemp);
void *pmem_map_fileW(const wchar_t *path, size_t len, int flags, mode_t mode,
	size_t *mapped_lenp, int *is_pmemp);
#endif

int pmem_unmap(void *addr, size_t len);
int pmem_is_pmem(const void *addr, size_t len);
void pmem_persist(const void *addr, size_t len);
int pmem_msync(const void *addr, size_t len);
void pmem_flush(const void *addr, size_t len);
void pmem_drain(void);
int pmem_has_hw_drain(void);
void *pmem_memmove_persist(void *pmemdest, const void *src, size_t len);
void *pmem_memcpy_persist(void *pmemdest, const void *src, size_t len);
void *pmem_memset_persist(void *pmemdest, int c, size_t len);
void *pmem_memmove_nodrain(void *pmemdest, const void *src, size_t len);
void *pmem_memcpy_nodrain(void *pmemdest, const void *src, size_t len);
void *pmem_memset_nodrain(void *pmemdest, int c, size_t len);

int pmem_poison_produce(void *addr, short addr_lsb);
void pmem_poison_consume(int (*handler)(void *addr, size_t len));

void pmem_poison_register_handler(void);

int pmem_poison_section(void *addr, size_t size, sigjmp_buf sigbus_jmpbuf);
void pmem_poison_section_end(void);

#define PMEM_POISON_HANDLE(addr, size)\
({sigjmp_buf sigbus_jmpbuf;\
int ret = sigsetjmp(sigbus_jmpbuf, 0);\
if (ret == 0) pmem_poison_section(addr, size, sigbus_jmpbuf);\
else pmem_poison_section_end();\
ret;\
})

#define PMEM_POISON_END()\
pmem_poison_section_end();

/*
 * PMEM_MAJOR_VERSION and PMEM_MINOR_VERSION provide the current version of the
 * libpmem API as provided by this header file.  Applications can verify that
 * the version available at run-time is compatible with the version used at
 * compile-time by passing these defines to pmem_check_version().
 */
#define PMEM_MAJOR_VERSION 1
#define PMEM_MINOR_VERSION 0

#ifndef _WIN32
const char *pmem_check_version(unsigned major_required,
	unsigned minor_required);
#else
const char *pmem_check_versionU(unsigned major_required,
	unsigned minor_required);
const wchar_t *pmem_check_versionW(unsigned major_required,
	unsigned minor_required);
#endif

#ifndef _WIN32
const char *pmem_errormsg(void);
#else
const char *pmem_errormsgU(void);
const wchar_t *pmem_errormsgW(void);
#endif

#ifdef __cplusplus
}
#endif
#endif	/* libpmem.h */
