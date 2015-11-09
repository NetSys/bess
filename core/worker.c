#include <rte_config.h>
#include <rte_cycles.h>
#include <rte_lcore.h>

#include <sched.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "common.h"
#include "worker.h"
#include "time.h"
#include "module.h"

int num_workers;
struct worker_context * volatile workers[MAX_WORKERS];
__thread struct worker_context ctx;

void set_non_worker()
{
	int socket;

	/* These TLS variables should not be accessed by the master thread.
	 * Assign INT_MIN to the variables so that the program can crash 
	 * when accessed as an index of an array. */
	ctx.wid = INT_MIN;
	ctx.core = INT_MIN;
	ctx.socket = INT_MIN;
	ctx.fd_event = INT_MIN;

	/* Packet pools should be available to non-worker threads */
	for (socket = 0; socket < RTE_MAX_NUMA_NODES; socket++) {
		struct rte_mempool *pool = get_lframe_pool_socket(socket);
		if (pool)
			ctx.lframe_pool = pool;
	}

	for (socket = 0; socket < RTE_MAX_NUMA_NODES; socket++) {
		struct rte_mempool *pool = get_pframe_pool_socket(socket);
		if (pool)
			ctx.pframe_pool = pool;
	}
}

int is_worker_core(int cpu)
{
	int wid;

	for (wid = 0; wid < MAX_WORKERS; wid++) {
		if (is_worker_active(wid) && workers[wid]->core == cpu)
			return 1;
	}

	return 0;
}

static void pause_worker(int wid)
{
	if (workers[wid] && workers[wid]->status == WORKER_RUNNING) {
		workers[wid]->status = WORKER_PAUSING;

		FULL_BARRIER();

		while (workers[wid]->status == WORKER_PAUSING)
			; 	/* spin */
	}
}

void pause_all_workers() 
{
	int i;

	for (i = 0; i < MAX_WORKERS; i++)
		pause_worker(i);
}

static void resume_worker(int wid)
{
	if (workers[wid]->status == WORKER_PAUSED) {
		uint64_t t = 1;
		int ret;

		ret = write(workers[wid]->fd_event, &t, sizeof(t));
		assert(ret == sizeof(t));

		while (workers[wid]->status == WORKER_PAUSED)
			; 	/* spin */
	}
}

void resume_all_workers() 
{
	process_orphan_tasks();

	for (int wid = 0; wid < MAX_WORKERS; wid++) {
		if (is_worker_active(wid))
			resume_worker(wid);
	}
}

int is_any_worker_running()
{
	int wid;

	for (wid = 0; wid < MAX_WORKERS; wid++) {
		if (workers[wid] && workers[wid]->status == WORKER_RUNNING)
			return 1;
	}

	return 0;
}

void block_worker()
{
	uint64_t t;
	int ret;

	ctx.status = WORKER_PAUSED;
	ret = read(ctx.fd_event, &t, sizeof(t));
	ctx.status = WORKER_RUNNING;
	assert(ret == sizeof(t));
}

/* arg is the core ID it should run on */
static int run_worker(void *arg)
{
	cpu_set_t set;
	int core;
	
	assert(ctx.status == WORKER_INACTIVE);

	core = (int)(int64_t)arg;

	CPU_ZERO(&set);
	CPU_SET(core, &set);
	rte_thread_set_affinity(&set);

	/* for workers, wid == rte_lcore_id() */
	ctx.wid = rte_lcore_id();
	ctx.core = core;
	ctx.socket = rte_socket_id();
	assert(ctx.socket >= 0);	/* shouldn't be SOCKET_ID_ANY (-1) */
	ctx.fd_event = eventfd(0, 0);
	assert(ctx.fd_event >= 0);

	ctx.s = sched_init();

	ctx.current_tsc = rdtsc();

	ctx.lframe_pool = get_lframe_pool();
	ctx.pframe_pool = get_pframe_pool();

	assert(ctx.lframe_pool);
	assert(ctx.pframe_pool);

	ctx.status = WORKER_PAUSING;

#if 0
	/* FIXME: when should this be called, avoiding latency */
	init_module_worker();
#endif

	STORE_BARRIER();
	workers[ctx.wid] = &ctx;

	printf("Worker %d(%p) is running on core %d (socket %d)\n", 
			ctx.wid, &ctx, ctx.core, ctx.socket);

	sched_loop(ctx.s);

	ctx.status = WORKER_INACTIVE;
	workers[ctx.wid] = NULL;
	STORE_BARRIER();

	return 0;
}

void launch_worker(int wid, int core)
{
	rte_eal_remote_launch(run_worker, (void *)(int64_t)core, wid);

	INST_BARRIER();

	while (!is_worker_active(wid) || workers[wid]->status != WORKER_PAUSED)
		;	/* spin until it becomes ready and fully paused */

	num_workers++;
}
