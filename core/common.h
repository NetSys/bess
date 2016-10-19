/* This header file contains general (not BESS specific) C definitions */

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include <x86intrin.h>

/* Hint for performance optimization. Same as _nassert() of TI compilers */
#define promise(cond)                     \
  ({                                      \
    if (!(cond)) __builtin_unreachable(); \
  })
#define promise_unreachable() __builtin_unreachable();

#ifndef likely
#  define likely(x) __builtin_expect((x), 1)
#endif

#ifndef unlikely
#  define unlikely(x) __builtin_expect((x), 0)
#endif

#define member_type(type, member) typeof(((type *)0)->member)

#define container_of(ptr, type, member)                  \
  ((type *)((char *)(member_type(type, member) *){ptr} - \
            offsetof(type, member)))

#define ARR_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

static inline uint64_t align_floor(uint64_t v, uint64_t align) {
  return v - (v % align);
}

static inline uint64_t align_ceil(uint64_t v, uint64_t align) {
  return align_floor(v + align - 1, align);
}

static inline uint64_t align_ceil_pow2(uint64_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;

  return v + 1;
}

/* err is defined as -errno,  */
static inline intptr_t ptr_to_err(const void *ptr) { return (intptr_t)ptr; }

static inline void *err_to_ptr(intptr_t err) { return (void *)err; }

static inline int is_err(const void *ptr) {
  const int max_errno = 4095;
  return (uintptr_t)ptr >= (uintptr_t)-max_errno;
}

static inline int is_err_or_null(const void *ptr) {
  return !ptr || is_err(ptr);
}

static inline int is_be_system() {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return 1;
#else
  return 0;
#endif
}

#define __cacheline_aligned __attribute__((aligned(64)))

/* For x86_64. DMA operations are not safe with these macros */
#define INST_BARRIER() asm volatile("" ::: "memory")
#define LOAD_BARRIER() INST_BARRIER()
#define STORE_BARRIER() INST_BARRIER()
#define FULL_BARRIER() asm volatile("mfence" ::: "memory")

/* src/dst addresses and their sizes must be a multiple of SIMD register size */
static inline void memcpy_sloppy(void *__restrict__ dst,
                                 const void *__restrict__ src, size_t n) {
#if __AVX2__
  typedef __m256i block_t;
#else
  typedef __m128i block_t;
#endif
  block_t *__restrict__ d = (block_t *)dst;
  const block_t *__restrict__ s = (const block_t *)src;

  int bytes_left = n;
  while (bytes_left > 0) {
    *d++ = *s++;
    bytes_left -= sizeof(block_t);
  }
}

// Put this in the declarations for a class to be uncopyable.
#define DISALLOW_COPY(TypeName) \
  TypeName(const TypeName&) = delete
// Put this in the declarations for a class to be unassignable.
#define DISALLOW_ASSIGN(TypeName) \
  void operator=(const TypeName&) = delete
// A macro to disallow the copy constructor and operator= functions.
// This should be used in the private: declarations for a class.
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete
// A macro to disallow all the implicit constructors, namely the
// default constructor, copy constructor and operator= functions.
//
// This should be used in the private: declarations for a class
// that wants to prevent anyone from instantiating it. This is
// especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName() = delete;                           \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

#endif
