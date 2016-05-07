/* This header file contains general (not BESS specific) C definitions */

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stddef.h>
#include <stdint.h>

#define ct_assert(p)	_Static_assert(p, "Compile-time assertion failure")

/* Hint for performance optimization. Same as _nassert() of TI compilers */
#define promise(cond) ({if (!(cond)) __builtin_unreachable(); })

#define member_type(type, member) typeof(((type *)0)->member)

#define container_of(ptr, type, member) \
	((type *)((char *)(member_type(type, member) *){ptr} - \
		offsetof(type, member)))

#define likely(x)	__builtin_expect((x),1)
#define unlikely(x)	__builtin_expect((x),0)

#define MIN(a, b) \
	({ \
		__typeof__ (a) _a = (a); \
		__typeof__ (b) _b = (b); \
	 	_a <= _b ? _a : _b; \
	 })

#define MAX(a, b) \
	({ \
		__typeof__ (a) _a = (a); \
		__typeof__ (b) _b = (b); \
	 	_a >= _b ? _a : _b; \
	 })

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

/* err is defined as -errno,  */
static inline intptr_t ptr_to_err(const void *ptr)
{
	return (intptr_t) ptr;
}

static inline void *err_to_ptr(intptr_t err)
{
	return (void *) err;
}

static inline int is_err(const void *ptr)
{
	const int max_errno = 4095;
	return (uintptr_t)ptr >= (uintptr_t)-max_errno;
}

static inline int is_err_or_null(const void *ptr)
{
	return !ptr || is_err(ptr);
}

#define __cacheline_aligned __attribute__((aligned(64)))

/* For x86_64. DMA operations are not safe with these macros */
#define INST_BARRIER()		asm volatile("" ::: "memory")
#define LOAD_BARRIER()		INST_BARRIER()
#define STORE_BARRIER()		INST_BARRIER()
#define FULL_BARRIER()		asm volatile("mfence":::"memory")

#endif
