#ifndef BESS_WORKER_H_
#define BESS_WORKER_H_

#include <cstdint>
#include <glog/logging.h>
#include <thread>

#include "gate.h"
#include "pktbatch.h"
#include "utils/common.h"

#define MAX_WORKERS 4

#define MAX_MODULES_PER_PATH 256

// XXX
typedef uint16_t gate_idx_t;
#define MAX_GATES 8192
#define BRANCH_FACTOR 3

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

typedef enum {
  WORKER_PAUSING = 0, /* transient state for blocking or quitting */
  WORKER_PAUSED,
  WORKER_RUNNING,
  WORKER_FINISHED,
} worker_status_t;

struct gate_task {
  bess::Gate *gate;
  struct pkt_batch batch;
};

class Worker {
 public:
  Worker()
      : wid_(),
        core_(),
        socket_(),
        fd_event_(),
        pframe_pool_(),
        s_(),
        silent_drops_(),
        current_tsc_(),
        current_ns_(),
        igate_stack_(),
        stack_depth_(),
        splits_() {}

  ~Worker() {}

  /* ----------------------------------------------------------------------
   * functions below are invoked by non-worker threads (the master)
   * ---------------------------------------------------------------------- */
  void SetNonWorker();

  /* ----------------------------------------------------------------------
   * functions below are invoked by worker threads
   * ---------------------------------------------------------------------- */
  inline int is_pause_requested() { return status_ == WORKER_PAUSING; }

  /* Block myself. Return nonzero if the worker needs to die */
  int Block();

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

  struct sched *s() {
    return s_;
  }

  uint64_t silent_drops() { return silent_drops_; }
  inline void set_silent_drops(uint64_t drops) { silent_drops_ = drops; }
  inline void incr_silent_drops(uint64_t drops) { silent_drops_ += drops; }

  uint64_t current_tsc() { return current_tsc_; }
  inline void set_current_tsc(uint64_t tsc) { current_tsc_ = tsc; }

  uint64_t current_ns() { return current_ns_; }
  inline void set_current_ns(uint64_t ns) { current_ns_ = ns; }

  /* The current input gate index is not given as a function parameter.
   * Modules should use get_igate() for access */
  gate_idx_t *igate_stack() { return igate_stack_; }
  int stack_depth() { return stack_depth_; }

  inline gate_idx_t igate_stack_top() { return igate_stack_[stack_depth_ - 1]; }

  inline void push_igate(gate_idx_t gate) {
    igate_stack_[stack_depth_] = gate;
    stack_depth_++;
  }

  inline gate_idx_t pop_igate() {
    stack_depth_--;
    return igate_stack_[stack_depth_];
  }

  // Store gate+packets into tasks for worker to service.
  // Returns true on success.
  inline bool push_ogate_and_packets(bess::Gate *gate, pkt_batch *batch) {
    if (pending_gates_ > MAX_MODULES_PER_PATH * BRANCH_FACTOR) {
      LOG(ERROR) << "Gate servicing stack overrun -- loop in execution?";
      return false;
    }
    struct gate_task *new_task = &(gate_servicing_stack_[pending_gates_]);
    new_task->gate = gate;     // store pointer
    new_task->batch = *batch;  // store value
    pending_gates_++;
    return true;
  }

  // Retrieve next gate that this worker should serve packets to.
  // Do not call without checking gates_pending() first.
  inline struct gate_task pop_ogate_and_packets() {
    pending_gates_--;
    return gate_servicing_stack_[pending_gates_];  // return value
  }

  inline bool gates_pending() { return !(pending_gates_ == 0); }

  /* better be the last field. it's huge */
  struct pkt_batch *splits() {
    return splits_;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(Worker);

  volatile worker_status_t status_;

  int wid_;  /* always [0, MAX_WORKERS - 1] */
  int core_; /* TODO: should be cpuset_t */
  int socket_;
  int fd_event_;

  struct rte_mempool *pframe_pool_;

  struct sched *s_;

  uint64_t silent_drops_; /* packets that have been sent to a deadend */

  uint64_t current_tsc_;
  uint64_t current_ns_;

  /* The current input gate index is not given as a function parameter.
   * Modules should use get_igate() for access */
  gate_idx_t igate_stack_[MAX_MODULES_PER_PATH];
  int stack_depth_;

  // Gates and packets that this worker should serve next.
  struct gate_task gate_servicing_stack_[MAX_MODULES_PER_PATH * BRANCH_FACTOR];
  int pending_gates_;

  /* better be the last field. it's huge */
  struct pkt_batch splits_[MAX_GATES + 1];
};

extern int num_workers;
extern std::thread worker_threads[MAX_WORKERS];
extern Worker *volatile workers[MAX_WORKERS];
extern thread_local Worker ctx;

/* ------------------------------------------------------------------------
 * functions below are invoked by non-worker threads (the master)
 * ------------------------------------------------------------------------ */
int is_worker_core(int cpu);

void pause_all_workers();
void resume_all_workers();
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

#endif  // BESS_WORKER_H_
