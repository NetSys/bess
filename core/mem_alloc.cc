#include "mem_alloc.h"

#define LIBC 0
#define DPDK 1

/* either LIBC or DPDK */
#define MEM_ALLOC_PROVIDER LIBC

#if MEM_ALLOC_PROVIDER == LIBC

#include <malloc.h>

#include <cstdlib>
#include <cstring>

void *mem_alloc(size_t size) {
  return calloc(1, size);
}

/* TODO: socket is ignored for now */
void *mem_alloc_ex(size_t size, size_t align, int) {
  void *ptr;
  int ret;

  ret = posix_memalign(&ptr, align, size);
  if (ret)
    return nullptr;

  memset(ptr, 0, size);

  return ptr;
}

void *mem_realloc(void *ptr, size_t size) {
  size_t old_size = malloc_usable_size(ptr);
  char *new_ptr = static_cast<char *>(realloc(ptr, size));

  if (new_ptr && size > old_size) {
    memset(new_ptr + old_size, 0, size - old_size);
  }

  return new_ptr;
}

void mem_free(void *ptr) {
  free(ptr);
}

#elif MEM_ALLOC_PROVIDER == DPDK

#include <rte_config.h>
#include <rte_malloc.h>

void *mem_alloc(size_t size) {
  return rte_zmalloc(/* name= */ nullptr, size, /* align= */ 0);
}

void *mem_realloc(void *ptr, size_t size) {
  return rte_realloc(ptr, size, /* align= */ 0);
}

void mem_free(void *ptr) {
  rte_free(ptr);
}

#else

#error "Unknown mem_alloc provider"

#endif
