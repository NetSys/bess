#ifndef BESS_WORKER_H_
#define BESS_WORKER_H_

#include <glog/logging.h>

#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>

#include "gate.h"
#include "pktbatch.h"
#include "traffic_class.h"
#include "utils/common.h"

#define MAX_WORKERS 4

#define MAX_MODULES_PER_PATH 256

// XXX
typedef uint16_t gate_idx_t;
#define MAX_GATES 8192

/*  TODO: worker threads doesn't necessarily be pinned to 1 core
 *
 *  n: MAX_WORKERS
 *
 *  Role              DPDK lcore ID      Hardware core(s)
 *  --------------------------------------------------------
 *  worker 0                      0      1 specified core
 *  worker 1                      1      1 specified core
 *  ...
 *  worker n-1                  n-1      1 specified core
 *  master          RTE_MAX_LCORE-1      all other cores that are allowed
 */

typedef enum {
  WORKER_PAUSING = 0, /* transient state for blocking or quitting */
  WORKER_PAUSED,
  WORKER_RUNNING,
  WORKER_FINISHED,
} worker_status_t;

namespace bess {
class Scheduler;
}  // namespace bess

class Worker {
 public:
  static const bess::TrafficPolicy kDefaultRootPolicy;
  static const char *kRootClassNamePrefix;
  static const char *kDefaultLeafClassNamePrefix;

  /* ----------------------------------------------------------------------
   * functions below are invoked by non-worker threads (the master)
   * ---------------------------------------------------------------------- */
  void SetNonWorker();

  /* ----------------------------------------------------------------------
   * functions below are invoked by worker threads
   * ---------------------------------------------------------------------- */
  inline int is_pause_requested() { return status_ == WORKER_PAUSING; }

  /* Block myself. Return nonzero if the worker needs to die */
  int BlockWorker();

  /* The entry point of worker threads */
  void *Run(void *_arg);

  worker_status_t status() { return status_; }
  void set_status(worker_status_t status) { status_ = status; }

  int wid() { return wid_; }
  int core() { return core_; }
  int socket() { return socket_; }
  int fd_event() { return fd_event_; }

  struct rte_mempool *pframe_pool() {
    return pframe_pool_;
  }

  bess::Scheduler *scheduler() { return scheduler_; }

  uint64_t silent_drops() { return silent_drops_; }
  void set_silent_drops(uint64_t drops) { silent_drops_ = drops; }
  void incr_silent_drops(uint64_t drops) { silent_drops_ += drops; }

  uint64_t current_tsc() const { return current_tsc_; }
  void set_current_tsc(uint64_t tsc) { current_tsc_ = tsc; }

  uint64_t current_ns() const { return current_ns_; }
  void set_current_ns(uint64_t ns) { current_ns_ = ns; }

  gate_idx_t current_igate() const { return current_igate_; }
  void set_current_igate(gate_idx_t idx) { current_igate_ = idx; }

  /* better be the last field. it's huge */
  bess::PacketBatch *splits() { return splits_; }

 private:
  volatile worker_status_t status_;

  int wid_;  /* always [0, MAX_WORKERS - 1] */
  int core_; /* TODO: should be cpuset_t */
  int socket_;
  int fd_event_;

  struct rte_mempool *pframe_pool_;

  bess::Scheduler *scheduler_;

  uint64_t silent_drops_; /* packets that have been sent to a deadend */

  uint64_t current_tsc_;
  uint64_t current_ns_;

  /* The current input gate index is not given as a function parameter.
   * Modules should use get_igate() for access */
  gate_idx_t current_igate_;

  /* better be the last field. it's huge */
  bess::PacketBatch splits_[MAX_GATES + 1];
};

// NOTE: Do not use "thread_local" here. It requires a function call every time
// it is accessed. Use __thread instead, which incurs minimal runtime overhead.
// For this, g++ requires Worker to have a trivial constructor and destructor.
extern __thread Worker ctx;

// the following traits are not supported in g++ 4.x
#if __GNUC__ >= 5
static_assert(std::is_trivially_constructible<Worker>::value,
              "not trivially constructible");
static_assert(std::is_trivially_destructible<Worker>::value,
              "not trivially destructible");
#endif

extern int num_workers;
extern std::thread worker_threads[MAX_WORKERS];
extern Worker *volatile workers[MAX_WORKERS];

/* ------------------------------------------------------------------------
 * functions below are invoked by non-worker threads (the master)
 * ------------------------------------------------------------------------ */
int is_worker_core(int cpu);

void pause_all_workers();
void resume_all_workers();
void destroy_worker(int wid);
void destroy_all_workers();

int is_any_worker_running();

int is_cpu_present(unsigned int core_id);

static inline int is_worker_active(int wid) {
  return workers[wid] != nullptr;
}

static inline int is_worker_running(int wid) {
  return workers[wid] && workers[wid]->status() == WORKER_RUNNING;
}

/* arg (int) is the core id the worker should run on */
void launch_worker(int wid, int core);

Worker *get_next_active_worker();

#endif  // BESS_WORKER_H_
