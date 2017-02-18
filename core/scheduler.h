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

struct ThrottledComp {
  inline bool operator()(const RateLimitTrafficClass *left,
                         const RateLimitTrafficClass *right) const {
    // Reversed so that priority_queue is a min priority queue.
    return right->throttle_expiration() < left->throttle_expiration();
  }
};

class Scheduler final {
 public:
  explicit Scheduler(TrafficClass *root, const std::string &leaf_name = "")
      : root_(root),
        default_leaf_class_(),
        throttled_cache_(ThrottledComp()),
        stats_(),
        checkpoint_(),
        ns_per_cycle_(1e9 / tsc_hz) {
    if (!leaf_name.empty()) {
      TrafficClass *c = TrafficClassBuilder::Find(leaf_name);
      CHECK(c);
      CHECK(c->policy() == POLICY_LEAF);
      default_leaf_class_ = static_cast<LeafTrafficClass *>(c);
    }
  }

  // TODO(barath): Do real cleanup, akin to sched_free() from the old impl.
  virtual ~Scheduler() {
    TrafficClassBuilder::Clear(root_);
    delete root_;
  }

  // Runs the scheduler loop forever.
  void ScheduleLoop();

  // Runs the scheduler once.
  void ScheduleOnce() __attribute__((always_inline)) {
    resource_arr_t usage;

    // Schedule.
    TrafficClass *c = Next(checkpoint_);

    uint64_t now;
    if (c) {
      ctx.set_current_tsc(checkpoint_);  // Tasks see updated tsc.
      ctx.set_current_ns(checkpoint_ * ns_per_cycle_);

      // Run.
      LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c);
      struct task_result ret = leaf->RunTasks();

      now = rdtsc();

      // Account.
      usage[RESOURCE_COUNT] = 1;
      usage[RESOURCE_CYCLE] = now - checkpoint_;
      usage[RESOURCE_PACKET] = ret.packets;
      usage[RESOURCE_BIT] = ret.bits;

      // TODO(barath): Re-enable scheduler-wide stats accumulation.
      // accumulate(stats_.usage, usage);

      leaf->FinishAndAccountTowardsRoot(this, nullptr, usage, now);
    } else {
      // TODO(barath): Ideally, we wouldn't spin in this case but rather take
      // the
      // fact that Next() returned nullptr as an indication that everything is
      // blocked, so we could wait until something is added that unblocks us. We
      // currently have no functionality to support such whole-scheduler
      // blocking/unblocking.
      ++stats_.cnt_idle;

      now = rdtsc();
      stats_.cycles_idle += (now - checkpoint_);
    }

    checkpoint_ = now;
  }

  // Adds the given rate limit traffic class to those that are considered
  // throttled (and need resuming later).
  void AddThrottled(RateLimitTrafficClass *rc) __attribute__((always_inline)) {
    throttled_cache_.push(rc);
  }

  // Selects the next TrafficClass to run.
  TrafficClass *Next(uint64_t tsc) __attribute__((always_inline)) {
    // Before we select the next class to run, resume any classes that were
    // throttled whose throttle time has expired so that they are available.
    ResumeThrottled(tsc);

    if (root_->blocked()) {
      // Nothing to schedule anywhere.
      return nullptr;
    }

    TrafficClass *c = root_;
    while (c->policy_ != POLICY_LEAF) {
      c = c->PickNextChild();
    }

    return c;
  }

  // Unthrottles any TrafficClasses that were throttled whose time has passed.
  void ResumeThrottled(uint64_t tsc) __attribute__((always_inline)) {
    while (!throttled_cache_.empty()) {
      RateLimitTrafficClass *rc = throttled_cache_.top();
      if (rc->throttle_expiration_ < tsc) {
        throttled_cache_.pop();
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

  LeafTrafficClass *default_leaf_class() { return default_leaf_class_; }

  // Sets the default leaf class, if it's unset. Returns true upon success.
  bool set_default_leaf_class(LeafTrafficClass *c) {
    if (default_leaf_class_) {
      return false;
    }

    default_leaf_class_ = c;
    return true;
  }

  // Return the number of traffic classes, excluding the root, managed by this
  // scheduler.
  size_t NumTcs() const {
    return root_->Size() - 1;
  }

 private:
  // Handles a rate limiter class's usage, and blocks it if needed.
  void HandleRateLimit(RateLimitTrafficClass *rc, uint64_t consumed,
                       uint64_t tsc);

  // Starts at the given class and attempts to unblock classes on the path
  // towards the root.
  void UnblockTowardsRoot(TrafficClass *c, uint64_t tsc);

  TrafficClass *root_;

  LeafTrafficClass *default_leaf_class_;

  // A cache of throttled TrafficClasses.
  std::priority_queue<RateLimitTrafficClass *,
                      std::vector<RateLimitTrafficClass *>, ThrottledComp>
      throttled_cache_;

  struct sched_stats stats_;
  uint64_t checkpoint_;

  double ns_per_cycle_;

  DISALLOW_COPY_AND_ASSIGN(Scheduler);
};

}  // namespace bess

#endif  // BESS_SCHEDULER_H_
