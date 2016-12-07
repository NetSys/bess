#include "scheduler.h"

#include "opts.h"
#include "traffic_class.h"
#include "utils/common.h"
#include "worker.h"

namespace bess {

void Scheduler::ScheduleLoop() {
  uint64_t now;
  // How many rounds to go before we do accounting.
  const uint64_t accounting_mask = 0xffff;
  static_assert(((accounting_mask + 1) & accounting_mask) == 0,
                "Accounting mask must be (2^n)-1");

  checkpoint_ = now = rdtsc();

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

void Scheduler::ScheduleOnce() {
  resource_arr_t usage;
  uint64_t now = rdtsc();

  // Schedule.
  TrafficClass *c = Next(now);

  if (c) {
    ctx.set_current_tsc(now);  // Tasks see updated tsc.
    ctx.set_current_ns(now * ns_per_cycle_);

    // Run.
    LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c);
    struct task_result ret = leaf->RunTasks();

    // Account.
    usage[RESOURCE_COUNT] = 1;
    usage[RESOURCE_CYCLE] = now - checkpoint_;
    usage[RESOURCE_PACKET] = ret.packets;
    usage[RESOURCE_BIT] = ret.bits;

    // TODO(barath): Re-enable scheduler-wide stats accumulation.
    // accumulate(stats_.usage, usage);

    leaf->FinishAndAccountTowardsRoot(this, nullptr, usage, now);
  } else {
    // TODO(barath): Ideally, we wouldn't spin in this case but rather take the
    // fact that Next() returned nullptr as an indication that everything is
    // blocked, so we could wait until something is added that unblocks us.  We
    // currently have no functionality to support such whole-scheduler
    // blocking/unblocking.
    ++stats_.cnt_idle;
    stats_.cycles_idle += (now - checkpoint_);
  }

  checkpoint_ = now;
}

TrafficClass *Scheduler::Next(uint64_t tsc) {
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

void Scheduler::AddThrottled(RateLimitTrafficClass *rc) {
  throttled_cache_.push(rc);
}

inline void Scheduler::ResumeThrottled(uint64_t tsc) {
  while (!throttled_cache_.empty()) {
    RateLimitTrafficClass *rc = throttled_cache_.top();
    if (rc->throttle_expiration_ < tsc) {
      throttled_cache_.pop();
      rc->throttle_expiration_ = 0;

      // Traverse upward toward root to unblock any blocked parents.
      rc->UnblockTowardsRoot(tsc);
    } else {
      break;
    }
  }
}

}  // namespace bess
