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

#ifndef BESS_SCHEDULER_H_
#define BESS_SCHEDULER_H_

#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#include "traffic_class.h"
#include "worker.h"

namespace bess {

struct sched_stats {
  resource_arr_t usage;
  uint64_t cnt_idle;
  uint64_t cycles_idle;
};

template <typename CallableTask>
class Scheduler;

// Queue of blocked traffic classes ordered by time expiration.
class SchedWakeupQueue {
 public:
  struct WakeupComp {
    bool operator()(const TrafficClass *left, const TrafficClass *right) const {
      // Reversed so that priority_queue is a min priority queue.
      return right->wakeup_time() < left->wakeup_time();
    }
  };

  SchedWakeupQueue() : q_(WakeupComp()) {}

  // Adds the given traffic class to those that are considered blocked.
  void Add(TrafficClass *c) { q_.push(c); }

 private:
  template <typename CallableTasks>
  friend class Scheduler;

  // A priority queue of TrafficClasses to wake up ordered by time.
  std::priority_queue<TrafficClass *, std::vector<TrafficClass *>, WakeupComp>
      q_;
};

// The non-instantiable base class for schedulers.  Implements common routines
// needed for scheduling.
template <typename CallableTask>
class Scheduler {
 public:
  explicit Scheduler(TrafficClass *root = nullptr)
      : root_(root),
        default_rr_class_(),
        wakeup_queue_(),
        stats_(),
        checkpoint_(),
        ns_per_cycle_(1e9 / tsc_hz) {}

  // TODO(barath): Do real cleanup, akin to sched_free() from the old impl.
  virtual ~Scheduler() {
    if (root_) {
      TrafficClassBuilder::Clear(root_);
    }
    delete root_;
  }

  // Runs the scheduler loop forever.
  virtual void ScheduleLoop() = 0;

  // Wakes up any TrafficClasses whose wakeup time has passed.
  void WakeTCs(uint64_t tsc) {
    while (!wakeup_queue_.q_.empty()) {
      TrafficClass *c = wakeup_queue_.q_.top();
      uint64_t wakeup_time = c->wakeup_time();
      if (wakeup_time < tsc) {
        wakeup_queue_.q_.pop();
        c->wakeup_time_ = 0;

        // Traverse upward toward root to unblock any blocked parents.
        c->UnblockTowardsRoot(wakeup_time);
      } else {
        break;
      }
    }
  }

  TrafficClass *root() { return root_; }

  // Add 'c' at the top of the scheduler's tree.  If the scheduler is empty,
  // 'c' becomes the root, otherwise it is be attached to a default
  // round-robin root.
  bool AttachOrphan(TrafficClass *c, int wid) {
    if (!root_) {
      root_ = c;
      return true;
    }
    if (default_rr_class_) {
      return default_rr_class_->AddChild(c);
    }
    default_rr_class_ =
        TrafficClassBuilder::CreateTrafficClass<RoundRobinTrafficClass>(
            std::string("!default_rr_") + std::to_string(wid));
    default_rr_class_->AddChild(root_);
    default_rr_class_->AddChild(c);
    root_ = default_rr_class_;
    return true;
  }

  // Simplify the root of the tree, removing an eventual default
  // round-robin root, if it has a single child (or none).
  void AdjustDefault() {
    if (!root_ || !default_rr_class_) {
      return;
    }

    const auto &children = default_rr_class_->Children();
    if (children.size() == 0) {
      delete root_;
      root_ = nullptr;
      default_rr_class_ = nullptr;
    } else if (children.size() == 1) {
      root_->RemoveChild(children[0]);
      delete root_;
      root_ = children[0];
      default_rr_class_ = nullptr;
    }
  }

  // If 'c' is the root of the scheduler's tree, remove it and return
  // true.  The caller now owns 'c'.
  bool RemoveRoot(TrafficClass *c) {
    if (root_ == c && default_rr_class_ == nullptr) {
      root_ = nullptr;
      return true;
    }
    return false;
  }

  // Return the number of traffic classes, managed by this scheduler.
  size_t NumTcs() const { return root_ ? root_->Size() : 0; }

  // For testing
  SchedWakeupQueue &wakeup_queue() { return wakeup_queue_; }

  // Selects the next TrafficClass to run.
  LeafTrafficClass<CallableTask> *Next(uint64_t tsc) {
    WakeTCs(tsc);

    if (!root_ || root_->blocked()) {
      // Nothing to schedule anywhere.
      return nullptr;
    }

    TrafficClass *c = root_;
    while (c->policy_ != POLICY_LEAF) {
      c = c->PickNextChild();
    }

    return static_cast<LeafTrafficClass<CallableTask> *>(c);
  }

 protected:
  // Starts at the given class and attempts to unblock classes on the path
  // towards the root.
  void UnblockTowardsRoot(TrafficClass *c, uint64_t tsc);

  TrafficClass *root_;

  RoundRobinTrafficClass *default_rr_class_;

  SchedWakeupQueue wakeup_queue_;

  struct sched_stats stats_;

  uint64_t checkpoint_;

  double ns_per_cycle_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Scheduler);
};

// The default scheduler, which picks the first leaf that the TC tree gives it
// and runs the corresponding task.
template <typename CallableTask>
class DefaultScheduler : public Scheduler<CallableTask> {
 public:
  explicit DefaultScheduler(TrafficClass *root = nullptr)
      : Scheduler<CallableTask>(root) {}

  virtual ~DefaultScheduler() {}

  // Runs the scheduler loop forever.
  void ScheduleLoop() override {
    uint64_t now;
    // How many rounds to go before we do accounting.
    const uint64_t accounting_mask = 0xff;
    static_assert(((accounting_mask + 1) & accounting_mask) == 0,
                  "Accounting mask must be (2^n)-1");

    this->checkpoint_ = now = rdtsc();

    // The main scheduling, running, accounting loop.
    for (uint64_t round = 0;; ++round) {
      // Periodic check, to mitigate expensive operations.
      if ((round & accounting_mask) == 0) {
        if (ctx.is_pause_requested()) {
          if (ctx.BlockWorker()) {
            break;
          }
        }
      }

      ScheduleOnce();
    }
  }

  // Runs the scheduler once.
  void ScheduleOnce() {
    resource_arr_t usage;

    // Schedule.
    LeafTrafficClass<CallableTask> *leaf =
        Scheduler<CallableTask>::Next(this->checkpoint_);

    uint64_t now;
    if (leaf) {
      ctx.set_current_tsc(this->checkpoint_);  // Tasks see updated tsc.
      ctx.set_current_ns(this->checkpoint_ * this->ns_per_cycle_);

      // Run.
      auto ret = leaf->task()();

      now = rdtsc();

      // Account.
      usage[RESOURCE_COUNT] = 1;
      usage[RESOURCE_CYCLE] = now - this->checkpoint_;
      usage[RESOURCE_PACKET] = ret.packets;
      usage[RESOURCE_BIT] = ret.bits;

      // TODO(barath): Re-enable scheduler-wide stats accumulation.
      // accumulate(stats_.usage, usage);

      leaf->FinishAndAccountTowardsRoot(&this->wakeup_queue_, nullptr, usage,
                                        now);
    } else {
      // TODO(barath): Ideally, we wouldn't spin in this case but rather take
      // the fact that Next() returned nullptr as an indication that everything
      // is blocked, so we could wait until something is added that unblocks us.
      // We currently have no functionality to support such whole-scheduler
      // blocking/unblocking.
      ++this->stats_.cnt_idle;

      now = rdtsc();
      this->stats_.cycles_idle += (now - this->checkpoint_);
    }

    this->checkpoint_ = now;
  }
};

template <typename CallableTask>
class ExperimentalScheduler : public Scheduler<CallableTask> {
 public:
  explicit ExperimentalScheduler(TrafficClass *root = nullptr)
      : Scheduler<CallableTask>(root) {}

  virtual ~ExperimentalScheduler() {}

  // Runs the scheduler loop forever.
  // Currently a copy-paste from DefaultScheduler so that this is the only
  // virtual call that is made (i.e., ScheduleOnce() is non-virtual).
  void ScheduleLoop() override {
    uint64_t now;
    // How many rounds to go before we do accounting.
    const uint64_t accounting_mask = 0xff;
    static_assert(((accounting_mask + 1) & accounting_mask) == 0,
                  "Accounting mask must be (2^n)-1");

    this->checkpoint_ = now = rdtsc();

    // The main scheduling, running, accounting loop.
    for (uint64_t round = 0;; ++round) {
      // Periodic check, to mitigate expensive operations.
      if ((round & accounting_mask) == 0) {
        if (ctx.is_pause_requested()) {
          if (ctx.BlockWorker()) {
            break;
          }
        }
      }

      ScheduleOnce();
    }
  }

  // Runs the scheduler once.
  void ScheduleOnce() {
    resource_arr_t usage;

    // Schedule.
    LeafTrafficClass<CallableTask> *leaf =
        Scheduler<CallableTask>::Next(this->checkpoint_);

    uint64_t now;
    if (leaf) {
      ctx.set_current_tsc(this->checkpoint_);  // Tasks see updated tsc.
      ctx.set_current_ns(this->checkpoint_ * this->ns_per_cycle_);

      // Run.
      auto ret = leaf->task()();
      now = rdtsc();

      if (ret.packets == 0 && ret.block) {
        constexpr uint64_t kMaxWait = 1ull << 32;
        uint64_t wait = std::min(kMaxWait, leaf->wait_cycles() << 1);
        leaf->set_wait_cycles(wait);

        leaf->blocked_ = true;
        leaf->wakeup_time_ = now + leaf->wait_cycles();
        this->wakeup_queue_.Add(leaf);

        usage[RESOURCE_COUNT] = 0;
        usage[RESOURCE_CYCLE] = 0;
        usage[RESOURCE_PACKET] = 0;
        usage[RESOURCE_BIT] = 0;
      } else {
        leaf->set_wait_cycles((leaf->wait_cycles() + 1) >> 1);

        usage[RESOURCE_COUNT] = 1;
        usage[RESOURCE_CYCLE] = now - this->checkpoint_;
        usage[RESOURCE_PACKET] = ret.packets;
        usage[RESOURCE_BIT] = ret.bits;
      }

      // Account.
      leaf->FinishAndAccountTowardsRoot(&this->wakeup_queue_, nullptr, usage,
                                        now);
    } else {
      ++this->stats_.cnt_idle;

      now = rdtsc();
      this->stats_.cycles_idle += (now - this->checkpoint_);
    }

    this->checkpoint_ = now;
  }
};

}  // namespace bess

#endif  // BESS_SCHEDULER_H_
