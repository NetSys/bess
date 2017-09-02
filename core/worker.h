// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

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
#include "utils/random.h"

// XXX
typedef uint16_t gate_idx_t;
#define MAX_GATES 8192

/*  TODO: worker threads doesn't necessarily be pinned to 1 core
 *
 *  n: kMaxWorkers
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
template <typename CallableTask>
class Scheduler;
}  // namespace bess

class Task;

class Worker {
 public:
  static const int kMaxWorkers = 64;
  static const int kAnyWorker = -1;  // unspecified worker ID

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

  bess::Scheduler<Task> *scheduler() { return scheduler_; }

  uint64_t silent_drops() { return silent_drops_; }
  void set_silent_drops(uint64_t drops) { silent_drops_ = drops; }
  void incr_silent_drops(uint64_t drops) { silent_drops_ += drops; }

  uint64_t current_tsc() const { return current_tsc_; }
  void set_current_tsc(uint64_t tsc) { current_tsc_ = tsc; }

  uint64_t current_ns() const { return current_ns_; }
  void set_current_ns(uint64_t ns) { current_ns_ = ns; }

  gate_idx_t current_igate() const { return current_igate_; }
  void set_current_igate(gate_idx_t idx) { current_igate_ = idx; }

  bess::PacketBatch **splits() { return splits_; }

  Random *rand() const { return rand_; }

 private:
  volatile worker_status_t status_;

  int wid_;   // always [0, kMaxWorkers - 1]
  int core_;  // TODO: should be cpuset_t
  int socket_;
  int fd_event_;

  struct rte_mempool *pframe_pool_;

  bess::Scheduler<Task> *scheduler_;

  uint64_t silent_drops_; /* packets that have been sent to a deadend */

  uint64_t current_tsc_;
  uint64_t current_ns_;

  /* The current input gate index is not given as a function parameter.
   * Modules should use get_igate() for access */
  gate_idx_t current_igate_;

  Random *rand_;

  // For each possible output gate contains a pointer to a batch, or nullptr,
  // if no batch has been associated with the output gate yet.
  //
  // This should be the last field, since it's huge.
  bess::PacketBatch *splits_[MAX_GATES + 1];
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

// TODO: C++-ify

extern int num_workers;
extern std::thread worker_threads[Worker::kMaxWorkers];
extern Worker *volatile workers[Worker::kMaxWorkers];

/* ------------------------------------------------------------------------
 * functions below are invoked by non-worker threads (the master)
 * ------------------------------------------------------------------------ */
int is_worker_core(int cpu);

void pause_worker(int wid);
void pause_all_workers();

/*!
 * Attach orphan TCs to workers. Note this does not ensure optimal placement.
 */
void attach_orphans();
void resume_worker(int wid);
void resume_all_workers();
void destroy_worker(int wid);
void destroy_all_workers();

bool is_any_worker_running();

int is_cpu_present(unsigned int core_id);

static inline int is_worker_active(int wid) {
  return workers[wid] != nullptr;
}

inline bool is_worker_running(int wid) {
  return workers[wid] && workers[wid]->status() == WORKER_RUNNING;
}

// arg (int) is the core id the worker should run on, and optionally the
// scheduler to use.
void launch_worker(int wid, int core, const std::string &scheduler = "");

Worker *get_next_active_worker();

// Add 'c' to the list of orphan traffic classes.
void add_tc_to_orphan(bess::TrafficClass *c, int wid);

// Return true if 'c' was removed from the list of orphan traffic classes.
// 'c' is now owned by the caller, and it must be attached to a tree or
// destroyed.
//
// Otherwise, return false
bool remove_tc_from_orphan(bess::TrafficClass *c);

// Returns a list of all the orphan traffic classes.
const std::list<std::pair<int, bess::TrafficClass *>> &list_orphan_tcs();

// Try to detach 'c' from a scheduler, or from the list of orhpan traffic
// classes.
//
// Return true if successful. 'c' is now owned by the caller, and it must be
// attached to a tree or destroyed.
//
// Otherwise, return false
bool detach_tc(bess::TrafficClass *c);

#endif  // BESS_WORKER_H_
