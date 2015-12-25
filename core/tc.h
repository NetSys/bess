#ifndef _TC_H_
#define _TC_H_

#include <string.h>

#include "common.h"
#include "namespace.h"

#include "utils/minheap.h"
#include "utils/cdlist.h"
#include "utils/simd.h"

#define SCHED_DEBUG		0

#define DEFAULT_PRIORITY	-1

#define MAX_LIMIT_POW		36
#define USAGE_AMPLIFIER_POW	32

enum {
	RESOURCE_CNT = 0,	/* how many times scheduled */
	RESOURCE_CYCLE,
	RESOURCE_PACKET,
	RESOURCE_BIT,
	NUM_RESOURCES,		/* Sentinel. Do not use. */
};

/* share is defined relatively, so 1024 should be large enough */
#define MAX_SHARE	(1 << 10)
#define STRIDE1		(1 << 20)

/* this doesn't mean anything, other than avoiding int64 overflow */
#define QUANTUM		(1 << 10)

typedef uint64_t resource_arr_t[NUM_RESOURCES] __ymm_aligned;

/* pgroup is a collection of sibling classes with the same priority */
struct pgroup {
	struct heap pq;

	int32_t priority;

	int resource;		/* [0, NUM_RESOURCES - 1] */
	int num_children;

	struct cdlist_item tc;
};

struct tc_params {
	char name[SN_NAME_LEN];

	struct tc *parent;

	/* Used for auto-generated TCs.
	 * (if its last task is detached, free the tc as well) */
	int auto_free;		

	int32_t priority;

	int32_t share;
	int share_resource;

	uint64_t limit[NUM_RESOURCES];	/* in work units per sec. 0 if unlimited */
	uint64_t max_burst[NUM_RESOURCES];
};

struct tc_stats {
	resource_arr_t usage;
	uint64_t cnt_throttled;
};

/***************************************************************************
 * Any change to the layout of this struct may affect performance.
 * Please group fields in a way that maximizes spatial cache locality.
 ***************************************************************************/
struct tc {
	/* NOTE: This counter is not atomic. 
	 * 1 by owner (the creator, or the scheduler if it is root), 
	 * 1 by ss.my_group->pq (when queued == 1), 
	 * 1 by s->pq (when throttled == 1), 
	 * m by its tasks, and n by children */
	uint32_t refcnt;

	int num_tasks;

	/* TODO: queued is somewhat redundant with runnable.
	 *       The status runnable==0 and queued==1 is basically
	 *       	an artifact of our minheap implementation that does not
	 *       	support remove operation other than pop(). */
	struct {
		int8_t runnable;	/* got work to do? */
		int8_t queued;		/* in the ss.my_pgroup->pq? */
		int8_t throttled;	/* being throttled (residing in s->pq) */
	} state;

	/* list of child pgroups (empty for leaf classes) */
	struct cdlist_head pgroups;	

	/* a TC performs round robin scheduling across its tasks */
	struct cdlist_head tasks;

	/****************************************************************
	 * Used for accounting only
	 ****************************************************************/
	struct tc *parent;		/* NULL for the root */

	uint64_t last_tsc; 		/* when was it last scheduled? */

	int has_limit;

	/* stride scheduling within the pgroup */
	struct {
		struct pgroup *my_pgroup; /* its parent pgroup */
		int64_t stride;
		int64_t pass;
		int64_t remain;
	} ss;

	struct tc_stats stats;

	/* For per-resource token buckets: 
	 * 1 work unit = 2 ^ USAGE_AMPLIFIER_POW resource usage.
	 * (for better precision without using floating point numbers) 
	 *
	 * prof->limit < 2^36 (~64 Tbps)
	 * 2^24 < tsc_hz < 2^34 (16 Mhz - 16 GHz)
	 * tb->limit < 2^36 */
	struct {
		/* how many work units per (10*9/hz) sec. 0 if unlimited */
		uint64_t limit;		
		uint64_t max_burst;	/* in work units */
		uint64_t tokens;	/* in work units */
	} tb[NUM_RESOURCES];

	/****************************************************************
	 * Not used in the "datapath" (sched_next or sched_done)
	 ****************************************************************/
	/* who is scheduling me? (NULL iff not attached) */
	struct sched *s;		

	char name[SN_NAME_LEN];

	int32_t priority;		/* the higher, the more important */
	int auto_free;			/* is this TC ephemeral? */

	/* linked list of all classes belonging to the same scheduler */
	struct cdlist_item sched_all;

	struct tc_stats last_stats;
};

struct sched_stats {
	resource_arr_t usage;
	uint64_t cnt_idle;
	uint64_t cycles_idle;
};

struct sched {
	struct tc root;			/* Must be the first field */
	struct tc *current;		/* currently running */

	/* priority queue of inactive (throttled) token buckets */
	struct heap pq;

	struct sched_stats stats;

	/* all traffic classes, except the root TC */
	int num_classes;
	struct cdlist_head tcs_all;
};

struct tc *tc_init(struct sched *s, const struct tc_params *prof);
void _tc_do_free(struct tc *c);

void tc_join(struct tc *c);
void tc_leave(struct tc *c);

static inline void tc_inc_refcnt(struct tc *c)
{
	c->refcnt++;
}

static inline void tc_dec_refcnt(struct tc *c)
{
	c->refcnt--;
	if (c->refcnt == 0)
		_tc_do_free(c);
}

struct sched *sched_init();
void sched_free(struct sched *s);

//struct tc *sched_next(struct sched *s);
//void sched_done(struct sched *s, const uint32_t *usage, int reschedule);

void sched_loop(struct sched *s);

void sched_test_alloc();
void sched_test_perf();

#endif
