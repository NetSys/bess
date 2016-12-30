/* a tiny shim layer to switch between rte_malloc and malloc
 * (or something else in the future) */

#ifndef BESS_MEMALLOC_H_
#define BESS_MEMALLOC_H_

#include <cstddef>

/* TODO: Use C++11-style allocator */

void *mem_alloc(size_t size); /* zero initialized by default */

void *mem_alloc_ex(size_t size, size_t align, int socket);

void *mem_realloc(void *ptr, size_t size);

void mem_free(void *ptr);

#endif  // BESS_MEMALLOC_H_
