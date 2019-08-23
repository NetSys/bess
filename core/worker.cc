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

#include "worker.h"

#include <sched.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <glog/logging.h>
#include <rte_config.h>
#include <rte_lcore.h>

#include <cassert>
#include <climits>
#include <list>
#include <string>
#include <utility>

#include "metadata.h"
#include "module.h"
#include "opts.h"
#include "packet_pool.h"
#include "resume_hook.h"
#include "resume_hooks/metadata.h"
#include "scheduler.h"
#include "utils/random.h"
#include "utils/time.h"

using bess::DefaultScheduler;
using bess::ExperimentalScheduler;
using bess::Scheduler;

int num_workers = 0;
std::thread worker_threads[Worker::kMaxWorkers];
Worker *volatile workers[Worker::kMaxWorkers];

using bess::TrafficClassBuilder;
using namespace bess::traffic_class_initializer_types;
using bess::ResumeHookBuilder;

std::list<std::pair<int, bess::TrafficClass *>> orphan_tcs;

// See worker.h
__thread Worker current_worker;

struct thread_arg {
  int wid;
  int core;
  Scheduler *scheduler;
};

#define SYS_CPU_DIR "/sys/devices/system/cpu/cpu%u"
#define CORE_ID_FILE "topology/core_id"

/* Check if a cpu is present by the presence of the cpu information for it */
int is_cpu_present(unsigned int core_id) {
  char path[PATH_MAX];
  int len = snprintf(path, sizeof(path), SYS_CPU_DIR "/" CORE_ID_FILE, core_id);
  if (len <= 0 || (unsigned)len >= sizeof(path)) {
    return 0;
  }
  if (access(path, F_OK) != 0) {
    return 0;
  }

  return 1;
}

int is_worker_core(int cpu) {
  int wid;

  for (wid = 0; wid < Worker::kMaxWorkers; wid++) {
    if (is_worker_active(wid) && workers[wid]->core() == cpu)
      return 1;
  }

  return 0;
}

void pause_worker(int wid) {
  if (workers[wid] && workers[wid]->status() == WORKER_RUNNING) {
    workers[wid]->set_status(WORKER_PAUSING);

    FULL_BARRIER();

    while (workers[wid]->status() == WORKER_PAUSING) {
    } /* spin */
  }
}

void pause_all_workers() {
  for (int wid = 0; wid < Worker::kMaxWorkers; wid++)
    pause_worker(wid);
}

enum class worker_signal : uint64_t {
  unblock = 1,
  quit,
};

void resume_worker(int wid) {
  if (workers[wid] && workers[wid]->status() == WORKER_PAUSED) {
    int ret;
    worker_signal sig = worker_signal::unblock;

    ret = write(workers[wid]->fd_event(), &sig, sizeof(sig));
    CHECK_EQ(ret, sizeof(uint64_t));

    while (workers[wid]->status() == WORKER_PAUSED) {
    } /* spin */
  }
}

/*!
 * Attach orphan TCs to workers. Note this does not ensure optimal placement.
 * This method can only be called when all workers are paused.
 */
void attach_orphans() {
  CHECK(!is_any_worker_running());
  // Distribute all orphan TCs to workers.
  for (const auto &tc : orphan_tcs) {
    bess::TrafficClass *c = tc.second;
    if (c->parent()) {
      continue;
    }

    Worker *w;

    int wid = tc.first;
    if (wid == Worker::kAnyWorker || workers[wid] == nullptr) {
      w = get_next_active_worker();
    } else {
      w = workers[wid];
    }

    w->scheduler()->AttachOrphan(c, w->wid());
  }

  orphan_tcs.clear();
}

void resume_all_workers() {
  for (int wid = 0; wid < Worker::kMaxWorkers; wid++) {
    if (workers[wid]) {
      workers[wid]->scheduler()->AdjustDefault();
    }
  }

  for (int wid = 0; wid < Worker::kMaxWorkers; wid++) {
    resume_worker(wid);
  }
}

void destroy_worker(int wid) {
  pause_worker(wid);

  if (workers[wid] && workers[wid]->status() == WORKER_PAUSED) {
    int ret;
    worker_signal sig = worker_signal::quit;

    ret = write(workers[wid]->fd_event(), &sig, sizeof(sig));
    CHECK_EQ(ret, sizeof(uint64_t));

    while (workers[wid]->status() == WORKER_PAUSED) {
    } /* spin */

    workers[wid] = nullptr;

    num_workers--;
  }

  if (num_workers > 0) {
    return;
  }

  auto &hooks = bess::global_resume_hooks;
  for (auto it = hooks.begin(); it != hooks.end();) {
    if ((*it)->is_default()) {
      it++;
    } else {
      it = hooks.erase(it);
    }
  }
}

void destroy_all_workers() {
  for (int wid = 0; wid < Worker::kMaxWorkers; wid++) {
    destroy_worker(wid);
  }
}

bool is_any_worker_running() {
  int wid;

  for (wid = 0; wid < Worker::kMaxWorkers; wid++) {
    if (is_worker_running(wid)) {
      return true;
    }
  }

  return false;
}

void Worker::SetNonWorker() {
  // These TLS variables should not be accessed by non-worker threads.
  // Assign INT_MIN to the variables so that the program can crash
  // when accessed as an index of an array.
  wid_ = INT_MIN;
  core_ = INT_MIN;
  socket_ = INT_MIN;
  fd_event_ = INT_MIN;

  if (!packet_pool_) {
    // Packet pools should be available to non-worker threads.
    // (doesn't need to be NUMA-aware, so pick any)
    for (int socket = 0; socket < RTE_MAX_NUMA_NODES; socket++) {
      if (bess::PacketPool *pool = bess::PacketPool::GetDefaultPool(socket)) {
        packet_pool_ = pool;
        break;
      }
    }
  }
}

int Worker::BlockWorker() {
  worker_signal t;
  int ret;

  status_ = WORKER_PAUSED;

  ret = read(fd_event_, &t, sizeof(t));
  CHECK_EQ(ret, sizeof(t));

  if (t == worker_signal::unblock) {
    status_ = WORKER_RUNNING;
    return 0;
  }

  if (t == worker_signal::quit) {
    status_ = WORKER_FINISHED;
    return 1;
  }

  CHECK(0);
  return 0;
}

/* The entry point of worker threads */
void *Worker::Run(void *_arg) {
  struct thread_arg *arg = (struct thread_arg *)_arg;
  rand_ = new Random();

  cpu_set_t set;

  CPU_ZERO(&set);
  CPU_SET(arg->core, &set);
  rte_thread_set_affinity(&set);

  /* DPDK lcore ID == worker ID (0, 1, 2, 3, ...) */
  RTE_PER_LCORE(_lcore_id) = arg->wid;

  /* for workers, wid == rte_lcore_id() */
  wid_ = arg->wid;
  core_ = arg->core;
  socket_ = rte_socket_id();

  // For some reason, rte_socket_id() does not return a correct NUMA ID.
  // Nevertheless, BESS should not crash.
  if (socket_ == SOCKET_ID_ANY) {
    LOG(WARNING) << "rte_socket_id() returned -1 for " << arg->core;
    socket_ = 0;
  }

  fd_event_ = eventfd(0, 0);
  CHECK_GE(fd_event_, 0);

  scheduler_ = arg->scheduler;

  current_tsc_ = rdtsc();

  packet_pool_ = bess::PacketPool::GetDefaultPool(socket_);
  CHECK_NOTNULL(packet_pool_);

  status_ = WORKER_PAUSING;

  STORE_BARRIER();

  workers[wid_] = this;  // FIXME: consider making workers a static member
                         // instead of a global

  LOG(INFO) << "Worker " << wid_ << "(" << this << ") "
            << "is running on core " << core_ << " (socket " << socket_ << ")";

  CPU_ZERO(&set);
  scheduler_->ScheduleLoop();

  LOG(INFO) << "Worker " << wid_ << "(" << this << ") "
            << "is quitting... (core " << core_ << ", socket " << socket_
            << ")";

  delete scheduler_;
  delete rand_;

  return nullptr;
}

void *run_worker(void *_arg) {
  CHECK_EQ(memcmp(&current_worker, new Worker(), sizeof(Worker)), 0);
  return current_worker.Run(_arg);
}

void launch_worker(int wid, int core,
                   [[maybe_unused]] const std::string &scheduler) {
  struct thread_arg arg = {.wid = wid, .core = core, .scheduler = nullptr};
  if (scheduler == "") {
    arg.scheduler = new DefaultScheduler();
  } else if (scheduler == "experimental") {
    arg.scheduler = new ExperimentalScheduler();
  } else {
    CHECK(false) << "Scheduler " << scheduler << " is invalid.";
  }

  worker_threads[wid] = std::thread(run_worker, &arg);
  worker_threads[wid].detach();

  INST_BARRIER();

  /* spin until it becomes ready and fully paused */
  while (!is_worker_active(wid) || workers[wid]->status() != WORKER_PAUSED) {
    continue;
  }

  num_workers++;
}

Worker *get_next_active_worker() {
  static int prev_wid = 0;
  if (num_workers == 0) {
    launch_worker(0, FLAGS_c);
    return workers[0];
  }

  while (!is_worker_active(prev_wid)) {
    prev_wid = (prev_wid + 1) % Worker::kMaxWorkers;
  }

  Worker *ret = workers[prev_wid];
  prev_wid = (prev_wid + 1) % Worker::kMaxWorkers;
  return ret;
}

void add_tc_to_orphan(bess::TrafficClass *c, int wid) {
  orphan_tcs.emplace_back(wid, c);
}

bool remove_tc_from_orphan(bess::TrafficClass *c) {
  for (auto it = orphan_tcs.begin(); it != orphan_tcs.end();) {
    if (it->second == c) {
      orphan_tcs.erase(it);
      return true;
    } else {
      it++;
    }
  }

  return false;
}

const std::list<std::pair<int, bess::TrafficClass *>> &list_orphan_tcs() {
  return orphan_tcs;
}

bool detach_tc(bess::TrafficClass *c) {
  bess::TrafficClass *parent = c->parent();
  if (parent) {
    return parent->RemoveChild(c);
  }

  // Try to remove from root of one of the schedulers
  for (int wid = 0; wid < Worker::kMaxWorkers; wid++) {
    if (workers[wid]) {
      bool found = workers[wid]->scheduler()->RemoveRoot(c);
      if (found) {
        return true;
      }
    }
  }

  // Try to remove from orphan_tcs
  return remove_tc_from_orphan(c);
}

WorkerPauser::WorkerPauser() {
  if (is_any_worker_running()) {
    for (int wid = 0; wid < Worker::kMaxWorkers; wid++) {
      if (is_worker_running(wid)) {
        workers_paused_.push_back(wid);
        VLOG(1) << "*** Pausing Worker " << wid << " ***";
        pause_worker(wid);
      }
    }
  }
}

WorkerPauser::~WorkerPauser() {
  attach_orphans();  // All workers should be paused at this point.

  if (!workers_paused_.empty()) {
    bess::run_global_resume_hooks(false);
  }

  std::set<Module *> modules_run;
  for (int wid : workers_paused_) {
    auto &resume_modules = bess::event_modules[bess::Event::PreResume];
    for (auto it = resume_modules.begin(); it != resume_modules.end();) {
      Module *m = *it;
      if (!modules_run.count(m) && m->active_workers()[wid]) {
        int ret = m->OnEvent(bess::Event::PreResume);
        modules_run.insert(m);
        if (ret == -ENOTSUP) {
          it = resume_modules.erase(it);
        } else {
          it++;
        }
      } else {
        it++;
      }
    }
    resume_worker(wid);
    VLOG(1) << "*** Worker " << wid << " Resumed ***";
  }
}
