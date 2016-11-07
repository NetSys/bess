#ifndef BESS_UTILS_MINHEAP_H_
#define BESS_UTILS_MINHEAP_H_

#include <cstdint>
#include <cstdlib>

#include "common.h"

/* NOTE: The real index starts from 1.
 *       The first and tail elements will be used as sentinel values. */
struct heap {
  uint32_t num_nodes;
  uint32_t size;
  int64_t *arr_v;
  void **arr_d;
};

void heap_init(struct heap *h);
void heap_close(struct heap *h);
void heap_grow(struct heap *h);

static inline void heap_push(struct heap *h, int64_t val, void *data) {
  int64_t *arr_v;
  void **arr_d;

  uint32_t i;

  if (unlikely(h->num_nodes == h->size)) {
    heap_grow(h);
  }

  arr_v = h->arr_v;
  arr_d = h->arr_d;

  h->num_nodes++;
  i = h->num_nodes;

  while (val < arr_v[i / 2]) {
    arr_v[i] = arr_v[i / 2];
    arr_d[i] = arr_d[i / 2];
    i = i / 2;
  }

  arr_v[i] = val;
  arr_d[i] = data;
}

static inline void *heap_peek(struct heap *h) {
  /* guaranteed to be nullptr if the heap is empty */
  return h->arr_d[1];
}

static inline void heap_peek_valdata(const struct heap *h, int64_t *ret_val,
                              void **ret_data) {
  *ret_val = h->arr_v[1];
  *ret_data = h->arr_d[1];
}

/* semantically identical to pop() followed by push() */
static inline void heap_replace(struct heap *h, int64_t val, void *data) {
  uint64_t i = 1;
  uint64_t c = 2;

  int64_t *arr_v = h->arr_v;
  void **arr_d = h->arr_d;

  for (;;) {
    int64_t lv = arr_v[c];
    int64_t rv = arr_v[c + 1];

    c += (lv > rv);

    if (val <= arr_v[c]) break;

    arr_v[i] = arr_v[c];
    arr_d[i] = arr_d[c];

    i = c;
    c = i * 2;
  }

  arr_d[i] = data;
  arr_v[i] = val;
}

static inline void heap_pop(struct heap *h) {
  uint32_t num_nodes = h->num_nodes;
  int64_t *arr_v = h->arr_v;
  void **arr_d = h->arr_d;

  /* of the replacing (the last) node */
  int64_t val;
  void *data;

  /* required for correctness */
  if (num_nodes == 1) {
    arr_v[1] = INT64_MAX;
    arr_d[1] = nullptr;
    h->num_nodes = 0;
    return;
  }

  val = arr_v[num_nodes];
  data = arr_d[num_nodes];
  arr_v[num_nodes] = INT64_MAX;
  arr_d[num_nodes] = nullptr;
  h->num_nodes = num_nodes - 1;

  heap_replace(h, val, data);
}

#endif  // BESS_UTILS_MINHEAP_H_
