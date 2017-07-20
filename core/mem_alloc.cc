// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

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
