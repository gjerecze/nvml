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
 * obj_pmalloc_mt.c -- multithreaded test of allocator
 */
#include <stdint.h>

#include "libpmemobj.h"
#include "pmalloc.h"
#include "unittest.h"

#define	ALLOC_SIZE 350
#define	ALLOC_HEADER 64

int
main(int argc, char *argv[])
{
	START(argc, argv, "obj_pmalloc_mt");

	if (argc < 2)
		FATAL("usage: %s [file]", argv[0]);


	PMEMobjpool *pop = pmemobj_create(argv[1], "TEST",
		PMEMOBJ_MIN_POOL, 0666);

	if (pop == NULL)
		FATAL("!pmemobj_create");

	heap_register_alloc_class(pop, ALLOC_SIZE + ALLOC_HEADER);

	PMEMoid oid;
	pmemobj_alloc(pop, &oid, ALLOC_SIZE, 0, NULL, NULL);
	ASSERT(!OID_IS_NULL(oid));

	pmemobj_close(pop);

	pop = pmemobj_open(argv[1], "TEST");
	ASSERTne(pop, NULL);

	ASSERT(OID_EQUALS(oid, pmemobj_first(pop, 0)));

	PMEMoid oid_copy = oid;

	pmemobj_free(&oid);
	pmemobj_alloc(pop, &oid, ALLOC_SIZE, 0, NULL, NULL);

	ASSERT(OID_EQUALS(oid, oid_copy));

	DONE(NULL);
}
