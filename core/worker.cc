#include "worker.h"

#include <cassert>
#include <climits>

#include <sched.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <glog/logging.h>
#include <rte_config.h>
#include <rte_lcore.h>

#include "metadata.h"
#include "packet.h"
#include "task.h"
#include "tc.h"
#include "utils/time.h"

int num_workers = 0;
std::thread worker_threads[MAX_WORKERS];
Worker *volatile workers[MAX_WORKERS];

// See worker.h
__thread Worker ctx;

struct thread_arg {
  int wid;
  int core;
};

#define SYS_CPU_DIR "/sys/devices/system/cpu/cpu%u"
#define CORE_ID_FILE "topology/core_id"

/* Check if a cpu is present by the presence of the cpu information for it */
int is_cpu_present(unsigned int core_id) {
  char path[PATH_MAX];
  int len = snprintf(path, sizeof(path), SYS_CPU_DIR "/" CORE_ID_FILE, core_id);
  if (len <= 0 || (unsigned)len >= sizeof(path))
    return 0;
  if (access(path, F_OK) != 0)
    return 0;

  return 1;
}

int is_worker_core(int cpu) {
  int wid;

  for (wid = 0; wid < MAX_WORKERS; wid++) {
    if (is_worker_active(wid) && workers[wid]->core() == cpu)
      return 1;
  }

  return 0;
}

static void pause_worker(int wid) {
  if (workers[wid] && workers[wid]->status() == WORKER_RUNNING) {
    workers[wid]->set_status(WORKER_PAUSING);

    FULL_BARRIER();

    while (workers[wid]->status() == WORKER_PAUSING)
      ; /* spin */
  }
}

void pause_all_workers() {
  for (int wid = 0; wid < MAX_WORKERS; wid++)
    pause_worker(wid);
}

enum class worker_signal : uint64_t {
  unblock = 1,
  quit,
};

static void resume_worker(int wid) {
  if (workers[wid] && workers[wid]->status() == WORKER_PAUSED) {
    int ret;
    worker_signal sig = worker_signal::unblock;

    ret = write(workers[wid]->fd_event(), &sig, sizeof(sig));
    assert(ret == sizeof(uint64_t));

    while (workers[wid]->status() == WORKER_PAUSED)
      ; /* spin */
  }
}

void resume_all_workers() {
  bess::metadata::default_pipeline.ComputeMetadataOffsets();
  process_orphan_tasks();

  for (int wid = 0; wid < MAX_WORKERS; wid++)
    resume_worker(wid);
}

static void destroy_worker(int wid) {
  pause_worker(wid);

  if (workers[wid] && workers[wid]->status() == WORKER_PAUSED) {
    int ret;
    worker_signal sig = worker_signal::quit;

    ret = write(workers[wid]->fd_event(), &sig, sizeof(sig));
    assert(ret == sizeof(uint64_t));

    while (workers[wid]->status() == WORKER_PAUSED)
      ; /* spin */

    workers[wid] = nullptr;

    num_workers--;
  }
}

void destroy_all_workers() {
  for (int wid = 0; wid < MAX_WORKERS; wid++)
    destroy_worker(wid);
}

int is_any_worker_running() {
  int wid;

  for (wid = 0; wid < MAX_WORKERS; wid++) {
    if (workers[wid] && workers[wid]->status() == WORKER_RUNNING)
      return 1;
  }

  return 0;
}

void Worker::SetNonWorker() {
  int socket;

  /* These TLS variables should not be accessed by the master thread.
   * Assign INT_MIN to the variables so that the program can crash
   * when accessed as an index of an array. */
  wid_ = INT_MIN;
  core_ = INT_MIN;
  socket_ = INT_MIN;
  fd_event_ = INT_MIN;

  /* Packet pools should be available to non-worker threads */
  for (socket = 0; socket < RTE_MAX_NUMA_NODES; socket++) {
    struct rte_mempool *pool = bess::get_pframe_pool_socket(socket);
    if (pool)
      pframe_pool_ = pool;
  }
}

int Worker::Block() {
  worker_signal t;
  int ret;

  status_ = WORKER_PAUSED;

  ret = read(fd_event_, &t, sizeof(t));
  assert(ret == sizeof(t));

  if (t == worker_signal::unblock) {
    status_ = WORKER_RUNNING;
    return 0;
  }

  if (t == worker_signal::quit) {
    status_ = WORKER_FINISHED;
    return 1;
  }

  assert(0);
}

/* The entry point of worker threads */
void *Worker::Run(void *_arg) {
  struct thread_arg *arg = (struct thread_arg *)_arg;

  cpu_set_t set;

  CPU_ZERO(&set);
  CPU_SET(arg->core, &set);
  rte_thread_set_affinity(&set);

  /* just in case */
  memset(this, 0, sizeof(*this));  // FIXME

  /* DPDK lcore ID == worker ID (0, 1, 2, 3, ...) */
  RTE_PER_LCORE(_lcore_id) = arg->wid;

  /* for workers, wid == rte_lcore_id() */
  wid_ = arg->wid;
  core_ = arg->core;
  socket_ = rte_socket_id();
  assert(socket_ >= 0); /* shouldn't be SOCKET_ID_ANY (-1) */
  fd_event_ = eventfd(0, 0);
  assert(fd_event_ >= 0);

  s_ = sched_init();

  current_tsc_ = rdtsc();

  pframe_pool_ = bess::get_pframe_pool();
  assert(pframe_pool_);

  status_ = WORKER_PAUSING;

  STORE_BARRIER();

  workers[wid_] = this;  // FIXME: consider making workers a static member
                         // instead of a global

  LOG(INFO) << "Worker " << wid_ << "(" << this << ") "
            << "is running on core " << core_ << " (socket " << socket_ << ")";

  CPU_ZERO(&set);
  sched_loop(s_);

  LOG(INFO) << "Worker " << wid_ << "(" << this << ") "
            << "is quitting... (core " << core_ << ", socket " << socket_
            << ")";

  sched_free(s_);

  return nullptr;
}

void *run_worker(void *_arg) {
  return ctx.Run(_arg);
}

void launch_worker(int wid, int core) {
  struct thread_arg arg = {.wid = wid, .core = core};
  worker_threads[wid] = std::thread(run_worker, &arg);
  worker_threads[wid].detach();

  INST_BARRIER();

  /* spin until it becomes ready and fully paused */
  while (!is_worker_active(wid) || workers[wid]->status() != WORKER_PAUSED)
    continue;

  num_workers++;
}
