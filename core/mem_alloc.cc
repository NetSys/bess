#include "mem_alloc.h"

#define LIBC			0
#define DPDK			1

/* either LIBC or DPDK */
#define MEM_ALLOC_PROVIDER	DPDK

#if MEM_ALLOC_PROVIDER == LIBC

#include <stdlib.h>
#include <string.h>

void *mem_alloc(size_t size)
{
	void *ptr = malloc(size);

	if (ptr)
		memset(ptr, 0, size);

	return ptr;
}

void *mem_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

void mem_free(void *ptr)
{
	free(ptr);
}

#elif MEM_ALLOC_PROVIDER == DPDK

#include <rte_config.h>
#include <rte_malloc.h>

void *mem_alloc(size_t size)
{
	return rte_zmalloc(/* name= */ NULL, size, /* align= */ 0);
}

void *mem_realloc(void *ptr, size_t size)
{
	return rte_realloc(ptr, size, /* align= */ 0);
}

void mem_free(void *ptr)
{
	rte_free(ptr);
}

#else

#error "Unknown mem_alloc provider"

#endif
