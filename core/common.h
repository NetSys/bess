#ifndef _COMMON_H_
#define _COMMON_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ct_assert(p)	_Static_assert(p, "Compile-time assertion failure")

/* XXX: add queue.h? */
typedef uint8_t queue_t;

#define QUEUE_UNKNOWN			255
#define MAX_QUEUES_PER_DIR		32	/* [0, 31] (for each RX/TX) */

ct_assert(MAX_QUEUES_PER_DIR < QUEUE_UNKNOWN);

#define MAX_WORKERS	4

extern const struct global_opts {
	uint16_t port;		/* TCP port for controller (0 for default) */
	int default_core;	/* Core ID for implicily launched worker */
	int foreground;		/* If 1, not daemonized */
	int kill_existing;	/* If 1, kill existing BESS instance */
	int print_tc_stats;	/* If 1, print TC stats every second */
	int debug_mode;		/* If 1, print control messages */
} global_opts;

/* The term RX/TX could be very confusing for a virtual switch.
 * Instead, we use the "incoming/outgoing" convention:
 * - incoming: outside -> BESS
 * - outgoing: BESS -> outside */
typedef enum {
	PACKET_DIR_INC 	= 0,
	PACKET_DIR_OUT 	= 1,
	PACKET_DIRS
} packet_dir_t;

#define member_type(type, member) typeof(((type *)0)->member)

#define container_of(ptr, type, member) \
	((type *)((char *)(member_type(type, member) *){ptr} - \
		offsetof(type, member)))

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

/* err is defined as -errno,  */
static inline int64_t ptr_to_err(const void *ptr)
{
	return (int64_t) ptr;
}

static inline void *err_to_ptr(int64_t err)
{
	return (void *) err;
}

static inline int is_err(const void *ptr)
{
	const int max_errno = 4095;
	return (uint64_t)ptr >= (uint64_t)-max_errno;
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
