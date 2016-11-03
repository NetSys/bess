#include "minheap.h"

#include "../mem_alloc.h"

void heap_init(struct heap *h) {
  uint32_t i;
  const uint32_t default_size = 4;

  h->size = default_size;
  h->num_nodes = 0;

  h->arr_v = (int64_t *)mem_alloc(sizeof(int64_t) * (h->size * 2 + 2));
  h->arr_d = (void **)mem_alloc(sizeof(void *) * (h->size * 2 + 2));

  h->arr_v[0] = INT64_MIN;
  h->arr_d[0] = nullptr;

  for (i = 1; i <= h->size * 2 + 1; i++) {
    h->arr_v[i] = INT64_MAX;
    h->arr_d[i] = nullptr;
  }
}

void heap_close(struct heap *h) {
  mem_free(h->arr_v);
  mem_free(h->arr_d);
}

void heap_grow(struct heap *h) {
  size_t array_size;

  h->size += h->size / 2; /* grow by 50% */
  array_size = h->size * 2 + 2;

  h->arr_v = (int64_t *)mem_realloc(h->arr_v, sizeof(int64_t) * array_size);
  h->arr_d = (void **)mem_realloc(h->arr_d, sizeof(void *) * array_size);

  for (uint32_t i = h->num_nodes * 2 + 2; i <= h->size * 2 + 1; i++) {
    h->arr_v[i] = INT64_MAX;
    h->arr_d[i] = nullptr;
  }
}
