#ifndef BESS_SCHEDULER_H_
#define BESS_SCHEDULER_H_

#include <iostream>
#include <queue>
#include <string>
#include <sstream>

#include "traffic_class.h"

namespace bess {

struct sched_stats {
  resource_arr_t usage;
  uint64_t cnt_idle;
  uint64_t cycles_idle;
};

struct ThrottledComp {
  inline bool operator()(const RateLimitTrafficClass *left, const RateLimitTrafficClass *right) const {
    // Reversed so that priority_queue is a min priority queue.
    return right->throttle_expiration() < left->throttle_expiration();
  }
};

class Scheduler final {
 public:
  Scheduler(TrafficClass *root, const std::string &leaf_name = "")
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
  void ScheduleOnce();

  // Adds the given rate limit traffic class to those that are considered
  // throttled (and need resuming later).
  void AddThrottled(RateLimitTrafficClass *rc);

  // Selects the next TrafficClass to run.
  TrafficClass *Next(uint64_t tsc);

  // Unthrottles any TrafficClasses that were throttled whose time has passed.
  inline void ResumeThrottled(uint64_t tsc);

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

 private:
  // Handles a rate limiter class's usage, and blocks it if needed.
  void HandleRateLimit(RateLimitTrafficClass *rc, uint64_t consumed, uint64_t tsc);

  // Starts at the given class and attempts to unblock classes on the path
  // towards the root.
  void UnblockTowardsRoot(TrafficClass *c, uint64_t tsc);

  TrafficClass *root_;

  LeafTrafficClass *default_leaf_class_;

  // A cache of throttled TrafficClasses.
  std::priority_queue<RateLimitTrafficClass *, std::vector<RateLimitTrafficClass *>, ThrottledComp> throttled_cache_;

  struct sched_stats stats_;
  uint64_t checkpoint_;

  double ns_per_cycle_;

  DISALLOW_COPY_AND_ASSIGN(Scheduler);
};

}  // namespace bess

#endif  // BESS_SCHEDULER_H_
