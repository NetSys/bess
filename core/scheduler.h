#ifndef BESS_SCHEDULER_H_
#define BESS_SCHEDULER_H_

#include <iostream>
#include <queue>
#include <sstream>
#include <stack>
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

enum class SchedulerType {
  FAST = 0,  // Corresponds to FastScheduler.
};

template <typename CallableTask>
class Scheduler;

// List of throttled traffic classes ordered by throttle expiration.
class SchedThrottledCache {
 public:
  struct ThrottledComp {
    inline bool operator()(const RateLimitTrafficClass *left,
                           const RateLimitTrafficClass *right) const {
      // Reversed so that priority_queue is a min priority queue.
      return right->throttle_expiration() < left->throttle_expiration();
    }
  };

  SchedThrottledCache() : q_(ThrottledComp()) {}
  // Adds the given rate limit traffic class to those that are considered
  // throttled (and need resuming later).
  void AddThrottled(RateLimitTrafficClass *rc) __attribute__((always_inline)) {
    q_.push(rc);
  }

 private:
  template <typename CallableTasks>
  friend class Scheduler;

  // A cache of throttled TrafficClasses.
  std::priority_queue<RateLimitTrafficClass *,
                      std::vector<RateLimitTrafficClass *>, ThrottledComp> q_;
};

// The non-instantiable base class for schedulers.  Implements common routines
// needed for scheduling.
template <typename CallableTask>
class Scheduler {
 public:
  explicit Scheduler(TrafficClass *root = nullptr)
      : root_(root),
        default_rr_class_(),
        throttled_cache_(),
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

  // Unthrottles any TrafficClasses that were throttled whose time has passed.
  void ResumeThrottled(uint64_t tsc) __attribute__((always_inline)) {
    while (!throttled_cache_.q_.empty()) {
      RateLimitTrafficClass *rc = throttled_cache_.q_.top();
      if (rc->throttle_expiration_ < tsc) {
        throttled_cache_.q_.pop();
        uint64_t expiration = rc->throttle_expiration_;
        rc->throttle_expiration_ = 0;

        // Traverse upward toward root to unblock any blocked parents.
        rc->UnblockTowardsRoot(expiration);
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

    size_t n_children = 0;
    TrafficClass *last_child = nullptr;
    default_rr_class_->TraverseChildren(
        [&last_child, &n_children](TCChildArgs *c) {
      last_child = c->child();
      n_children++;
    });

    if (n_children <= 1) {
      root_ = last_child;
      if (root_) {
        default_rr_class_->RemoveChild(root_);
      }
      delete default_rr_class_;
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
  size_t NumTcs() const {
    return root_ ? root_->Size() : 0;
  }

  // For testing
  SchedThrottledCache &throttled_cache() {
      return throttled_cache_;
  }

 protected:
  // Handles a rate limiter class's usage, and blocks it if needed.
  void HandleRateLimit(RateLimitTrafficClass *rc, uint64_t consumed,
                       uint64_t tsc);

  // Starts at the given class and attempts to unblock classes on the path
  // towards the root.
  void UnblockTowardsRoot(TrafficClass *c, uint64_t tsc);

  TrafficClass *root_;

  RoundRobinTrafficClass *default_rr_class_;

  // A cache of throttled TrafficClasses.
  SchedThrottledCache throttled_cache_;

  struct sched_stats stats_;
  uint64_t checkpoint_;

  double ns_per_cycle_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Scheduler);
};

// The default "fast" scheduler, which picks the first leaf that the TC tree
// gives it and runs the corresponding task.
template <typename CallableTask>
class FastScheduler : public Scheduler<CallableTask> {
 public:
  explicit FastScheduler(TrafficClass *root = nullptr) : Scheduler<CallableTask>(root) {}

  virtual ~FastScheduler() {}

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
  void ScheduleOnce() __attribute__((always_inline)) {
    resource_arr_t usage;

    // Schedule.
    LeafTrafficClass<CallableTask> *leaf = Next(this->checkpoint_);

    uint64_t now;
    if (leaf) {
      ctx.set_current_tsc(this->checkpoint_);  // Tasks see updated tsc.
      ctx.set_current_ns(this->checkpoint_ * this->ns_per_cycle_);

      // Run.
      auto ret = leaf->Task()();

      now = rdtsc();

      // Account.
      usage[RESOURCE_COUNT] = 1;
      usage[RESOURCE_CYCLE] = now - this->checkpoint_;
      usage[RESOURCE_PACKET] = ret.packets;
      usage[RESOURCE_BIT] = ret.bits;

      // TODO(barath): Re-enable scheduler-wide stats accumulation.
      // accumulate(stats_.usage, usage);

      leaf->FinishAndAccountTowardsRoot(&this->throttled_cache_, nullptr, usage, now);
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

  // Selects the next TrafficClass to run.
  LeafTrafficClass<CallableTask> *Next(uint64_t tsc)
      __attribute__((always_inline)) {
    // Before we select the next class to run, resume any classes that were
    // throttled whose throttle time has expired so that they are available.
    this->ResumeThrottled(tsc);

    if (!this->root_ || this->root_->blocked()) {
      // Nothing to schedule anywhere.
      return nullptr;
    }

    TrafficClass *c = this->root_;
    while (c->policy_ != POLICY_LEAF) {
      c = c->PickNextChild();
    }

    return static_cast<LeafTrafficClass<CallableTask> *>(c);
  }
};

}  // namespace bess

#endif  // BESS_SCHEDULER_H_
