#ifndef BESS_SNOBJ_H_
#define BESS_SNOBJ_H_

/* NOTE: this library is not thread-safe */

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mem_alloc.h"
#include "utils/common.h"

#define MAX_EXPR_LEN 128 /* including the trailing nullptr */

#define CHECK_DOUBLE_FREE 0

enum snobj_type {
  TYPE_NIL = 0, /* must be zero. useful for boolean flags */
  TYPE_INT,     /* signed or unsigned 64-bit integer */
  TYPE_DOUBLE,  /* double-precision floating-point numbers */
  TYPE_STR,     /* null-terminated strings (size includes \0) */
  TYPE_BLOB,
  TYPE_LIST,
  TYPE_MAP,
};

/* In C99 we cannot specify enum size. Instead, use uint*_t directly */
typedef uint32_t snobj_type_t;

/* keep it to 32B for optimal memory usage */
struct snobj {
  snobj_type_t type;

  uint32_t refcnt;

  /* INT: always 8,
   * BLOB: bytes (including \0 for string)
   * LIST, MAP: number of elements
   * (using size_t here is an overkill...) */
  uint32_t size;

  /* used internally for maps and lists */
  uint32_t max_size;
  union {
    int64_t int_value;
    double double_value;

    void *data;

    struct {
      struct snobj **arr;
    } list;

    /* currently implemented with arrays and
     * linear search :(
     * is hash table really necessary? */
    struct {
      char **arr_k;
      struct snobj **arr_v;
    } map;
  };
} __attribute__((aligned(32)));

/* this function does not return (always succeeds) */
static inline void *_ALLOC(size_t size) {
  void *ret = mem_alloc(size);
  if (!ret) {
    abort();
  }

  return ret;
}

static inline void *_REALLOC(void *p, size_t new_size) {
  void *ret = mem_realloc(p, new_size);
  if (!ret) {
    abort();
  }

  return ret;
}

#if CHECK_DOUBLE_FREE
#include <assert.h>
#endif

/* should not crash when p == nullptr */
static inline void _FREE(void *p) {
#if CHECK_DOUBLE_FREE
  const uint64_t magic_number = 0xc1fa23e79b0486d5UL;
  uint64_t *t = p;

  if (*t == magic_number)
    assert(0); /* Found (potential double free!) */
  else
    *t = magic_number;
  ;
#endif
  mem_free(p);
}

static inline char *_STRDUP(const char *s) {
  size_t len = strlen(s);
  char *ret;

  ret = (char *)_ALLOC(len + 1);
  memcpy(ret, s, len + 1);

  return ret;
}

/*
 * Reference counting:
 * 1. when struct snobj is allocated, its refcnt is 1 (the holder is "you")
 * 2. snobj_free() decreases the refcnt by 1
 * 3. you give up your ref when you call snobj_list_add() or snobj_map_set()
 *    (if you want to keep using the child, call snobj_acquire() beforehand)
 * 4. snobj_*_get() or snobj_eval_*() do NOT affect its refcnt.
 */

static inline void snobj_acquire(struct snobj *m) { m->refcnt++; }

void snobj_free(struct snobj *m);

/* allocators */
struct snobj *snobj_nil(void);
struct snobj *snobj_int(int64_t v);
struct snobj *snobj_uint(uint64_t v);
struct snobj *snobj_double(double v);
struct snobj *snobj_blob(const void *data, size_t size); /* data is copied */
struct snobj *snobj_str(const char *str);                /* str is copied */
struct snobj *snobj_str(const std::string &str);
struct snobj *snobj_str_fmt(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
struct snobj *snobj_list(void);
struct snobj *snobj_map(void);

int snobj_list_add(struct snobj *m, struct snobj *child);
int snobj_list_del(struct snobj *m, size_t index);

static inline int64_t snobj_int_get(const struct snobj *m) {
  if (m->type != TYPE_INT) return 0;

  return m->int_value;
}

static inline uint64_t snobj_uint_get(const struct snobj *m) {
  return (uint64_t)snobj_int_get(m);
}

static inline double snobj_double_get(const struct snobj *m) {
  if (m->type != TYPE_DOUBLE) return NAN;

  return m->double_value;
}

static inline double snobj_number_get(const struct snobj *m) {
  if (m->type == TYPE_INT) return (double)m->int_value;

  if (m->type == TYPE_DOUBLE) return m->double_value;

  return NAN;
}

static inline char *snobj_str_get(const struct snobj *m) {
  if (m->type != TYPE_STR) return nullptr;

  return (char *)m->data;
}

static inline void *snobj_blob_get(const struct snobj *m) {
  if (m->type != TYPE_BLOB) return nullptr;

  return (void *)m->data;
}

static inline struct snobj *snobj_list_get(const struct snobj *m,
                                           uint32_t idx) {
  if (m->type != TYPE_LIST) return nullptr;

  if (idx >= m->size) return nullptr;

  return m->list.arr[idx];
}

struct snobj *snobj_map_get(const struct snobj *m, const char *key);
int snobj_map_set(struct snobj *m, const char *key, struct snobj *val);

int snobj_binvalue_get(struct snobj *m, uint32_t size, void *dst, int force_be);

static inline snobj_type_t snobj_type(const struct snobj *m) { return m->type; }

static inline size_t snobj_size(const struct snobj *m) { return m->size; }

/* expr can be recursive (e.g., foo.bar[3].baz).
 * returns nullptr if not found */
struct snobj *snobj_eval(const struct snobj *m, const char *expr);

/* snobj_eval_* return 0, NAN, or nullptr if the key is not found */
static inline int64_t snobj_eval_int(const struct snobj *m, const char *expr) {
  m = snobj_eval(m, expr);

  return m ? snobj_int_get(m) : 0;
}

static inline uint64_t snobj_eval_uint(const struct snobj *m,
                                       const char *expr) {
  return (uint64_t)snobj_eval_int(m, expr);
}

static inline double snobj_eval_double(const struct snobj *m,
                                       const char *expr) {
  m = snobj_eval(m, expr);

  return m ? snobj_double_get(m) : NAN;
}

static inline char *snobj_eval_str(const struct snobj *m, const char *expr) {
  m = snobj_eval(m, expr);

  return m ? snobj_str_get(m) : nullptr;
}

static inline void *snobj_eval_blob(const struct snobj *m, const char *expr) {
  m = snobj_eval(m, expr);

  return m ? snobj_blob_get(m) : nullptr;
}

static inline int snobj_eval_exists(const struct snobj *m, const char *expr) {
  return snobj_eval(m, expr) != nullptr;
}

std::string snobj_dump(const struct snobj *m);

/* recursive encoding of TYPE(4B), SIZE(4B), DATA(variable)
 * DATA is 8-byte aligned by tail padding with zeroes.
 * returns the size of the newly created buffer (always a multiple of 8)
 * returns 0 if failed.
 * hint is used as initial buffer size, but doesn't need to be accurate */
size_t snobj_encode(const struct snobj *m, char **pbuf, size_t hint);

struct snobj *snobj_decode(char *buf, size_t buf_size);

/* helper function for the common error message format */
struct snobj *__attribute__((format(printf, 3, 4)))
snobj_err_details(int err, struct snobj *details, const char *fmt, ...);

#define snobj_err(err, fmt, ...) \
  snobj_err_details(err, nullptr, fmt, ##__VA_ARGS__)

struct snobj *snobj_errno(int err);

struct snobj *snobj_errno_details(int err, struct snobj *details);

#endif  // BESS_SNOBJ_H_
