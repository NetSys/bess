#include <sched.h>
#include <unistd.h>
#include <limits.h>
#include <sys/eventfd.h>

#include <rte_config.h>
#include <rte_cycles.h>
#include <rte_lcore.h>

#include "common.h"
#include "worker.h"
#include "time.h"
#include "module.h"

int num_workers;
struct worker_context * volatile workers[MAX_WORKERS];
__thread struct worker_context ctx;

#define SYS_CPU_DIR "/sys/devices/system/cpu/cpu%u"
#define CORE_ID_FILE "topology/core_id"

/* Check if a cpu is present by the presence of the cpu information for it */
int is_cpu_present(unsigned int core_id)
{
       char path[PATH_MAX];
       int len = snprintf(path, sizeof(path), SYS_CPU_DIR
               "/"CORE_ID_FILE, core_id);
       if (len <= 0 || (unsigned)len >= sizeof(path))
               return 0;
       if (access(path, F_OK) != 0)
               return 0;

       return 1;
}

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
	for (int wid = 0; wid < MAX_WORKERS; wid++)
		pause_worker(wid);
}

#define SIGNAL_UNBLOCK	1
#define SIGNAL_QUIT	2

static void resume_worker(int wid)
{
	if (workers[wid] && workers[wid]->status == WORKER_PAUSED) {
		int ret;

		ret = write(workers[wid]->fd_event, &(uint64_t){SIGNAL_UNBLOCK}, 
				sizeof(uint64_t));
		assert(ret == sizeof(uint64_t));

		while (workers[wid]->status == WORKER_PAUSED)
			; 	/* spin */
	}
}

void resume_all_workers() 
{
	process_orphan_tasks();

	for (int wid = 0; wid < MAX_WORKERS; wid++)
		resume_worker(wid);
}

static void destroy_worker(int wid)
{
	pause_worker(wid);

	if (workers[wid] && workers[wid]->status == WORKER_PAUSED) {
		int ret;

		ret = write(workers[wid]->fd_event, &(uint64_t){SIGNAL_QUIT}, 
				sizeof(uint64_t));
		assert(ret == sizeof(uint64_t));

		while (workers[wid])
			; 	/* spin */

		num_workers--;
	}

	rte_eal_wait_lcore(wid);
}

void destroy_all_workers()
{
	for (int wid = 0; wid < MAX_WORKERS; wid++)
		destroy_worker(wid);
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

int block_worker()
{
	uint64_t t;
	int ret;

	ctx.status = WORKER_PAUSED;

	ret = read(ctx.fd_event, &t, sizeof(t));
	assert(ret == sizeof(t));

	if (t == SIGNAL_UNBLOCK)
		ctx.status = WORKER_RUNNING;
	else if (t == SIGNAL_QUIT)
		return 1;
	else
		assert(0);

	return 0;
}

/* arg is the core ID it should run on */
static int run_worker(void *arg)
{
	cpu_set_t set;
	int core;

	core = (int)(int64_t)arg;

	CPU_ZERO(&set);
	CPU_SET(core, &set);
	rte_thread_set_affinity(&set);

	/* just in case */
	memset(&ctx, 0, sizeof(ctx));

	/* for workers, wid == rte_lcore_id() */
	ctx.wid = rte_lcore_id();
	ctx.core = core;
	ctx.socket = rte_socket_id();
	assert(ctx.socket >= 0);	/* shouldn't be SOCKET_ID_ANY (-1) */
	ctx.fd_event = eventfd(0, 0);
	assert(ctx.fd_event >= 0);

	ctx.s = sched_init();

	ctx.current_tsc = rdtsc();

	ctx.pframe_pool = get_pframe_pool();
	assert(ctx.pframe_pool);

	ctx.status = WORKER_PAUSING;

#if 0
	/* FIXME: when should this be called, avoiding latency */
	init_module_worker();
#endif

	STORE_BARRIER();
	workers[ctx.wid] = &ctx;

	log_info("Worker %d(%p) is running on core %d (socket %d)\n", 
			ctx.wid, &ctx, ctx.core, ctx.socket);

	CPU_ZERO(&set);
	sched_loop(ctx.s);

	log_info("Worker %d(%p) is quitting... (core %d, socket %d)\n", 
			ctx.wid, &ctx, ctx.core, ctx.socket);

	sched_free(ctx.s);

	STORE_BARRIER();
	workers[ctx.wid] = NULL;

	return 0;
}

void launch_worker(int wid, int core)
{
	int ret;

	ret = rte_eal_remote_launch(run_worker, (void *)(int64_t)core, wid);
	assert(ret == 0);

	INST_BARRIER();

	while (!is_worker_active(wid) || workers[wid]->status != WORKER_PAUSED)
		;	/* spin until it becomes ready and fully paused */

	num_workers++;
}
