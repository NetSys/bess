#ifndef _WORKER_H_
#define _WORKER_H_

#include <stdint.h>

#include "common.h"
#include "mclass.h"
#include "pktbatch.h"

#define MAX_MODULES_PER_PATH	256

/* 	TODO: worker threads doesn't necessarily be pinned to 1 core
 *
 *  	n: MAX_WORKERS
 *
 *  	Role		DPDK lcore ID		Hardware core(s)
 *  	--------------------------------------------------------
 *  	worker 0	0			1 specified core
 *	worker 1	1			1 specified core
 *	...
 *	worker n-1	n-1			1 specified core
 *	master		RTE_MAX_LCORE-1		all other cores
 */

typedef volatile enum {
	WORKER_PAUSING = 0,	/* transient state for blocking or quitting */
	WORKER_PAUSED,
	WORKER_RUNNING,
} worker_status_t;

struct worker_context {
	worker_status_t status;

	int wid;		/* always [0, MAX_WORKERS - 1] */
	int core;		/* TODO: should be cpuset_t */
	int socket;
	int fd_event;

	struct rte_mempool *pframe_pool;

	struct sched *s;

	uint64_t silent_drops;	/* packets that have been sent to a deadend */

	uint64_t current_tsc;
	uint64_t current_us;

	/* The current input gate index is not given as a function parameter.
	 * Modules should use get_igate() for access */
	gate_idx_t igate_stack[MAX_MODULES_PER_PATH];
	int stack_depth;
	
	/* better be the last field. it's huge */
	struct pkt_batch splits[MAX_GATES + 1];
};

extern int num_workers;
extern struct worker_context * volatile workers[MAX_WORKERS];
extern __thread struct worker_context ctx;

/* ------------------------------------------------------------------------
 * functions below are invoked by non-worker threads (the master)
 * ------------------------------------------------------------------------ */
void set_non_worker();

int is_worker_core(int cpu);

void pause_all_workers();
void resume_all_workers();
void destroy_all_workers();

int is_any_worker_running();

int is_cpu_present(unsigned int core_id);

/* arg (int) is the core id the worker should run on */
void launch_worker(int wid, int core);	

static inline int is_worker_active(int wid)
{
	return (workers[wid] != NULL);
}

static inline int is_worker_running(int wid)
{
	return (workers[wid] && workers[wid]->status == WORKER_RUNNING);
}

/* ------------------------------------------------------------------------
 * functions below are invoked by worker threads
 * ------------------------------------------------------------------------ */
static inline int is_pause_requested() {
	return (ctx.status == WORKER_PAUSING);
}

/* Block myself. Return nonzero if the worker needs to die */
int block_worker(void);	

#endif
