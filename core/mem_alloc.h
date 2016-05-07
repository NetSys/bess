/* a tiny shim layer to switch between rte_malloc and malloc 
 * (or something else in the future) */

#ifndef _MEM_ALLOC_H_
#define _MEM_ALLOC_H_

#include <stddef.h>

void *mem_alloc(size_t size);	/* zero initialized by default */
void *mem_realloc(void *ptr, size_t size);
void mem_free(void *ptr);

/* void *mem_alloc_ex(size_t size, size_t align, int socket); */

#endif
