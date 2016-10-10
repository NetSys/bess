#include <sched.h>
#include <unistd.h>
#include <limits.h>

#include <sys/eventfd.h>

#include <rte_config.h>
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
               "/" CORE_ID_FILE, core_id);
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
	if (workers[wid] && workers[wid]->status == worker_context::running) {
		workers[wid]->status = worker_context::pausing;

		FULL_BARRIER();

		while (workers[wid]->status == worker_context::pausing)
			; 	/* spin */
	}
}

void pause_all_workers() 
{
	for (int wid = 0; wid < MAX_WORKERS; wid++)
		pause_worker(wid);
}

enum class worker_signal : uint64_t {
	unblock = 1,
	quit,
};

static void resume_worker(int wid)
{
	if (workers[wid] && workers[wid]->status == worker_context::paused) {
		int ret;
		worker_signal sig = worker_signal::unblock;

		ret = write(workers[wid]->fd_event, &sig, sizeof(sig));
		assert(ret == sizeof(uint64_t));

		while (workers[wid]->status == worker_context::paused)
			; 	/* spin */
	}
}

void resume_all_workers()
{
	compute_metadata_offsets();
	process_orphan_tasks();

	for (int wid = 0; wid < MAX_WORKERS; wid++)
		resume_worker(wid);
}

static void destroy_worker(int wid)
{
	pause_worker(wid);

	if (workers[wid] && workers[wid]->status == worker_context::paused) {
		int ret;
		worker_signal sig = worker_signal::quit;

		ret = write(workers[wid]->fd_event, &sig, sizeof(sig));
		assert(ret == sizeof(uint64_t));

		ret = pthread_join(workers[wid]->thread, NULL);
		assert(ret == 0);

		workers[wid] = NULL;

		num_workers--;
	}
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
		if (workers[wid] && workers[wid]->status == worker_context::running)
			return 1;
	}

	return 0;
}

int block_worker()
{
	worker_signal t;
	int ret;

	ctx.status = worker_context::paused;

	ret = read(ctx.fd_event, &t, sizeof(t));
	assert(ret == sizeof(t));

	if (t == worker_signal::unblock) {
		ctx.status = worker_context::running;
		return 0;
	}

	if (t == worker_signal::quit) {
		ctx.status = worker_context::running;
		return 1;
	}

	assert(0);
}

struct thread_arg {
	int wid;
	int core;
};

/* The entry point of worker threads */
static void *run_worker(void *_arg)
{
	struct thread_arg *arg = (struct thread_arg *)_arg;

	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(arg->core, &set);
	rte_thread_set_affinity(&set);

	/* just in case */
	memset(&ctx, 0, sizeof(ctx));

	/* DPDK lcore ID == worker ID (0, 1, 2, 3, ...) */
	RTE_PER_LCORE(_lcore_id) = arg->wid;

	/* for workers, wid == rte_lcore_id() */
	ctx.thread = pthread_self();
	ctx.wid = arg->wid;
	ctx.core = arg->core;
	ctx.socket = rte_socket_id();
	assert(ctx.socket >= 0);	/* shouldn't be SOCKET_ID_ANY (-1) */
	ctx.fd_event = eventfd(0, 0);
	assert(ctx.fd_event >= 0);

	ctx.s = sched_init();

	ctx.current_tsc = rdtsc();

	ctx.pframe_pool = get_pframe_pool();
	assert(ctx.pframe_pool);

	ctx.status = worker_context::pausing;

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

	return NULL;
}

void launch_worker(int wid, int core)
{
	pthread_t thread;
	struct thread_arg arg = {.wid = wid, .core = core};
	int ret;

	ret = pthread_create(&thread, NULL, run_worker, &arg);
	assert(ret == 0);

	INST_BARRIER();

	/* spin until it becomes ready and fully paused */
	while (!is_worker_active(wid) || workers[wid]->status != worker_context::paused)
		continue;

	num_workers++;
}
