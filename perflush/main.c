/*
 * Copyright 2017, Intel Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <assert.h>
#include <time.h>
#include <emmintrin.h>
#include <immintrin.h>

static int omp_enabled;

#define ASIZE(array)  (sizeof(array) / sizeof((array)[0]))
#define ALIGN(val, alignment) ((val) + ((alignment)-1) & ((alignment) - 1))
#define FSIZE (1ULL << 31) /* 1 gigabyte */
#define SEED (1234)
#define DATA (5)

#define _mm_clflushopt(addr)\
asm volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)(addr)));
#define _mm_clwb(addr)\
asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)(addr)));

/*
 * 32-bits Random number generator U[0,1): lfsr113
 * Author: Pierre L'Ecuyer,
 * Source: http://www.iro.umontreal.ca/~lecuyer/myftp/papers/tausme2.ps
*/
static unsigned int
randf(void)
{
	static unsigned int z1 = SEED, z2 = SEED, z3 = SEED, z4 = SEED;
	unsigned int b;
	b  = ((z1 << 6) ^ z1) >> 13;
	z1 = ((z1 & 4294967294U) << 18) ^ b;
	b  = ((z2 << 2) ^ z2) >> 27;
	z2 = ((z2 & 4294967288U) << 2) ^ b;
	b  = ((z3 << 13) ^ z3) >> 21;
	z3 = ((z3 & 4294967280U) << 7) ^ b;
	b  = ((z4 << 3) ^ z4) >> 12;
	z4 = ((z4 & 4294967168U) << 13) ^ b;
	return (z1 ^ z2 ^ z3 ^ z4);
}

#define CACHELINE 64

static char *random_data;

static int
clflush_seq(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += CACHELINE) {
			char *caddr = (char *)(addr + i);
			*caddr = DATA;
			_mm_clflush(caddr);
		}
	}

	return 0;
}

static int
clflush_rand(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += CACHELINE) {
			size_t n = ((randf() % FSIZE) / data_size) * data_size;
			char *caddr = (char *)(addr + n);
			*caddr = DATA;
			_mm_clflush(caddr);
		}
	}

	return 0;
}

static int
clflush_static(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += CACHELINE) {
			char *caddr = (char *)(addr);
			*caddr = DATA;
			_mm_clflush(caddr);
		}
	}

	return 0;
}

static int
clflushopt_seq(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			for (size_t j = i; j < i + data_size; j += CACHELINE) {
				char *caddr = (char *)(addr + j);
				*caddr = DATA;
				_mm_clflushopt(caddr);
			}
			_mm_sfence();
		}
	}

	return 0;
}

static int
clflushopt_rand(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			for (size_t j = i; j < i + data_size; j += CACHELINE) {
				size_t n = ((randf() % FSIZE) / data_size) * data_size;
				char *caddr = (char *)(addr + n);
				*caddr = DATA;
				_mm_clflushopt(caddr);
			}
			_mm_sfence();
		}
	}

	return 0;
}

static int
clflushopt_static(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			for (size_t j = i; j < i + data_size; j += CACHELINE) {
				char *caddr = (char *)(addr);
				*caddr = DATA;
				_mm_clflushopt(caddr);
			}
			_mm_sfence();
		}
	}

	return 0;
}

/*
 * ntmemcpy -- trivial non-temporal aligned memcpy
 */
static void
ntmemcpy(void *dest, void *src, size_t size)
{
#define CHUNK_SHIFT	7
#define CHUNK_COUNT     8
	__m128i *d = (__m128i *)dest;
	__m128i *s = (__m128i *)src;
	__m128i xmm[CHUNK_COUNT];

	for (size_t i = 0; i < size >> CHUNK_SHIFT; i++) {
		for (int j = 0; j < CHUNK_COUNT; ++j)
			xmm[j] = _mm_loadu_si128(s++);
		for (int j = 0; j < CHUNK_COUNT; ++j)
			_mm_stream_si128(d++, xmm[j]);
	}

	_mm_sfence();
#undef CHUNK_COUNT
#undef CHUNK_SHIFT
}

/*
 * ntmemset -- trivial non-temporal aligned memset
 */
static void
ntmemset(void *dest, int value, size_t size)
{
#define CHUNK_SHIFT	7
#define CHUNK_COUNT     8
	__m128i s = _mm_set1_epi8((char)value);
	__m128i *d = (__m128i *)dest;

	for (size_t i = 0; i < size >> CHUNK_SHIFT; i++) {
		for (int j = 0; j < CHUNK_COUNT; ++j)
			_mm_stream_si128(d++, s);
	}

	_mm_sfence();
#undef CHUNK_COUNT
#undef CHUNK_SHIFT
}

/*
 * ntmemcpy512 -- trivial non-temporal aligned memcpy
 */
static void
ntmemcpy512(void *dest, void *src, size_t size)
{
#define CHUNK_SHIFT	9
#define CHUNK_COUNT     8
	__m512i *d = (__m512i *)dest;
	__m512i *s = (__m512i *)src;
	__m512i xmm[CHUNK_COUNT];

	for (size_t i = 0; i < size >> CHUNK_SHIFT; i++) {
		for (int j = 0; j < CHUNK_COUNT; ++j)
			xmm[j] = _mm512_loadu_si512(s++);
		for (int j = 0; j < CHUNK_COUNT; ++j)
			_mm512_stream_si512(d++, xmm[j]);
	}

	_mm_sfence();
#undef CHUNK_COUNT
#undef CHUNK_SHIFT
}

/*
 * ntmemset512 -- trivial non-temporal aligned memset
 */
static void
ntmemset512(void *dest, int value, size_t size)
{
#define CHUNK_SHIFT	9
#define CHUNK_COUNT     8
	__m512i *d = (__m512i *)dest;
	__m512i s = _mm512_set1_epi32((char)value);

	for (size_t i = 0; i < size >> CHUNK_SHIFT; i++) {
		for (int j = 0; j < CHUNK_COUNT; ++j)
			_mm512_stream_si512(d++, s);
	}

	_mm_sfence();
#undef CHUNK_COUNT
#undef CHUNK_SHIFT
}

static int
ntmemcpy512_noflush(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			char *caddr = (char *)(addr + i);
			ntmemcpy512(caddr, random_data + i, data_size);
		}
	}

	return 0;
}

static int
ntmemset512_noflush(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			char *caddr = (char *)(addr + i);
			ntmemset512(caddr, 'c', data_size);
		}
	}

	return 0;
}

static int
memcpy_noflush(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			char *caddr = (char *)(addr + i);
			memcpy(caddr, random_data + i, data_size);
		}
	}

	return 0;
}

static int
ntmemcpy_noflush(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			char *caddr = (char *)(addr + i);
			ntmemcpy(caddr, random_data + i, data_size);
		}
	}

	return 0;
}

static int
ntmemset_noflush(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			char *caddr = (char *)(addr + i);
			ntmemset(caddr, 'c', data_size);
		}
	}

	return 0;
}

static int
ntmemcpy_clflush(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			char *caddr = (char *)(addr + i);
			ntmemcpy(caddr, random_data + i, data_size);
			for (size_t j = i; j < data_size; j += CACHELINE) {
				char *faddr = (char *)(addr + j);
				_mm_clflush(faddr);
			}
		}
	}

	return 0;
}

static int
ntmemcpy_clflushopt(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			char *caddr = (char *)(addr + i);
			ntmemcpy(caddr, random_data + i, data_size);
			for (size_t j = i; j < data_size; j += CACHELINE) {
				char *faddr = (char *)(addr + j);
				_mm_clflushopt(faddr);
			}
			_mm_sfence();
		}
	}

	return 0;
}

static int
read_flushed(void *addr, size_t data_size)
{
	char *buf = malloc(data_size);
	assert(buf != NULL);
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += data_size) {
			for (size_t j = i; j < data_size; j += CACHELINE) {
				char *faddr = (char *)(addr + j);
				_mm_clflushopt(faddr);
			}
			_mm_sfence();

			char *caddr = (char *)(addr + i);
			ntmemcpy(buf, caddr, data_size);
		}
	}

	free(buf);

	return 0;
}

static int
ntstore(void *addr, size_t data_size)
{
	#pragma omp parallel if (omp_enabled)
	{
		size_t i = 0;
		#pragma omp for private(i)
		for (i = 0; i < FSIZE; i += CACHELINE) {
			long long *caddr = (long long *)(addr);
			_mm_stream_si64(caddr, DATA);
			_mm_sfence();
		}
	}

	return 0;
}

typedef int scenario_func(void *addr, size_t data_size);

struct scenario {
	char *name;
	scenario_func *func;
};

#define STR(name) #name
#define SCENARIO(func)\
{STR(func), func}

static struct scenario scenarios[] = {
	SCENARIO(clflush_seq),
	SCENARIO(clflush_rand),
	SCENARIO(clflush_static),
	SCENARIO(clflushopt_seq),
	SCENARIO(clflushopt_rand),
	SCENARIO(clflushopt_static),
	SCENARIO(ntmemcpy_noflush),
	SCENARIO(ntmemcpy_clflush),
	SCENARIO(ntmemcpy_clflushopt),
	SCENARIO(read_flushed),
	SCENARIO(ntstore),
	SCENARIO(ntmemcpy512_noflush),
	SCENARIO(memcpy_noflush),
	SCENARIO(ntmemset_noflush),
	SCENARIO(ntmemset512_noflush),
	{NULL, NULL}
};

/*
 * exec_scenario -- runs a scenario with a given name
 */
static int
exec_scenario(char *name, void *addr, size_t data_size)
{
	for (struct scenario *s = &scenarios[0]; s->name != NULL; ++s) {
		if (strcmp(s->name, name) == 0)
			return s->func(addr, data_size);
	}

	printf("scenario %s not found\n", name);
	return -1;
}

/*
 * fill -- warmup
 */
static void
fill(void *addr)
{
	memset(addr, 0xc, FSIZE);
	_mm_sfence();
}

int
main(int argc, char *argv[])
{
	if (argc < 4) {
		printf("usage: %s path scenario data_size [omp?]\n", argv[0]);
		return -1;
	}
	char *path = argv[1];
	char *scenario = argv[2];
	size_t data_size = (size_t)atoi(argv[3]);
	if (argc == 5)
		omp_enabled = atoi(argv[4]) == 1 ? 1 : 0;

	int fd = open(path, O_RDWR);
	assert(fd != -1);
	void *addr = mmap(NULL, FSIZE, PROT_READ|PROT_WRITE,
		MAP_SHARED|MAP_NORESERVE|MAP_POPULATE, fd, 0);
	assert(addr != MAP_FAILED);

	fill(addr);

	/* used for memcpy tests */
	random_data = malloc(FSIZE);
	assert(random_data != NULL);
	for (size_t i = 0; i < FSIZE; ++i) {
		/* not really random, but doesn't matter */
		*(random_data + i) = (char)i;
	}

	struct timespec start, stop;
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	int ret = exec_scenario(scenario, addr, data_size);
	clock_gettime(CLOCK_MONOTONIC_RAW, &stop);

	if (ret == 0) {
		double time = ((double)stop.tv_sec + 1.0e-9 * stop.tv_nsec) -
			((double)start.tv_sec + 1.0e-9 * start.tv_nsec);

		/* time in seconds */
		float gigabytes = FSIZE / time / 1024 / 1024 / 1024;

		printf("%fs scenario runtime\n%f gigabytes per second\n", time,
			gigabytes);
	}

	munmap(addr, FSIZE);
	close(fd);

	return 0;
}
