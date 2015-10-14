#ifndef _WORKER_H_
#define _WORKER_H_

#include <stdint.h>

#define MAX_WORKERS	4

/* 	TODO: worker threads doesn't necessarily be pinned to 1 core
 *
 *  	n: initial num_workers
 *
 *  	Role		DPDK lcore ID		Hardware core(s)
 *  	--------------------------------------------------------
 *  	worker 0	0			1 specified core
 *	worker 1	1			1 specified core
 *	...
 *	worker n-1	n-1			1 specified core
 *	idle threads	[n, RTE_MAX_LCORE-2]	all other cores
 *	master		RTE_MAX_LCORE-1		all other cores
 *
 *	Idle threads will be used for dynamic worker allocation.
 *	(not supported yet)
 */

typedef volatile enum {
	WORKER_INACTIVE = 0,
	WORKER_RUNNING,
	WORKER_PAUSING,		/* transient state */
	WORKER_PAUSED,
} worker_status_t;

struct worker_context {
	worker_status_t status;

	int wid;		/* always [0, MAX_WORKERS - 1] */
	int core;		/* TODO: should be cpuset_t */
	int socket;
	int fd_event;

	struct sched *s;

	uint64_t silent_drops;	/* packets that have been sent to a deadend */

	uint64_t current_tsc;
	uint64_t current_us;

	struct rte_mempool *pframe_pool;
	struct rte_mempool *lframe_pool;
};

extern int num_workers;
extern struct worker_context * volatile workers[MAX_WORKERS];
extern __thread struct worker_context ctx;

/* ------------------------------------------------------------------------
 * functions below are invoked by non-worker threads (the master)
 * ------------------------------------------------------------------------ */
void set_non_worker();

int is_worker_core(int cpu);

void pause_worker(int wid);
void pause_all_workers();
void resume_worker(int wid);
void resume_all_workers();

int is_any_worker_running();

/* arg (int) is the core id the worker should run on */
void launch_worker(int wid, int core);	

static inline int is_worker_active(int wid)
{
	return (workers[wid] && workers[wid]->status != WORKER_INACTIVE);
}

/* ------------------------------------------------------------------------
 * functions below are invoked by worker threads
 * ------------------------------------------------------------------------ */
static inline int is_pause_requested() {
	return (ctx.status == WORKER_PAUSING);
}

void block_worker(void);	/* block myself */

#endif
