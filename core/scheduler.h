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

class Scheduler {
 public:
  // By default names for schedulers' root classes are the pointer of the
  // scheduler object, for debugging convenience.
  Scheduler()
      : root_([](Scheduler *s){std::stringstream ss; ss << s; return ss.str();}(this)),
        throttled_cache_(ThrottledComp()),
        stats_(),
        last_stats_(),
        last_print_tsc_(),
        checkpoint_(),
        now_(),
        ns_per_cycle_(1e9 / tsc_hz) {}

  // TODO(barath): Do real cleanup, akin to sched_free() from the old impl.
  virtual ~Scheduler() {}

  // Runs the scheduler loop forever.
  void ScheduleLoop();

  // Runs the scheduler once.
  void ScheduleOnce();

  PriorityTrafficClass *root() { return &root_; }

 private:
  // Selects the next TrafficClass to run.
  TrafficClass *Next();

  // Finishes the scheduling of a TrafficClass, to be called after Next() to
  // clean up.
  void Done(TrafficClass *c, resource_arr_t usage, uint64_t tsc);

  // Handles a rate limiter class's usage, and blocks it if needed.
  void HandleRateLimit(RateLimitTrafficClass *rc, uint64_t consumed, uint64_t tsc);

  // Starts at the given class and attempts to unblock classes on the path
  // towards the root.
  void UnblockTowardsRoot(TrafficClass *c, uint64_t tsc);

  // Unthrottles any TrafficClasses that were throttled whose time has passed.
  void ResumeThrottled(uint64_t tsc);

  PriorityTrafficClass root_;

  // A cache of throttled TrafficClasses.
  std::priority_queue<RateLimitTrafficClass *, std::vector<RateLimitTrafficClass *>, ThrottledComp> throttled_cache_;

  struct sched_stats stats_;
  struct sched_stats last_stats_;
  uint64_t last_print_tsc_;
  uint64_t checkpoint_;
  uint64_t now_;

  double ns_per_cycle_;

  DISALLOW_COPY_AND_ASSIGN(Scheduler);
};

}  // namespace bess

#endif  // BESS_SCHEDULER_H_
