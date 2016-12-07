#include "scheduler.h"

#include "opts.h"
#include "traffic_class.h"
#include "utils/common.h"
#include "worker.h"

namespace bess {

void Scheduler::ScheduleLoop() {
  // How many rounds to go before we do accounting.
  const uint64_t accounting_mask = 0xffff; 
  static_assert(((accounting_mask+1) & accounting_mask) == 0,
                "Accounting mask must be (2^n)-1");

  last_stats_ = stats_;
  last_print_tsc_ = checkpoint_ = now_ = rdtsc();

  ApplyToAllClasses([=](TrafficClass *c){c->set_last_tsc(now_);});

  // The main scheduling, running, accounting loop.
  for (uint64_t round = 0;; ++round) {
    // Periodic check, to mitigate expensive operations.
    if (unlikely((round & accounting_mask) == 0)) {
      if (unlikely(ctx.is_pause_requested())) {
        if (ctx.BlockWorker()) {
          // TODO(barath): Add log message here?
          break;
        }
        last_stats_ = stats_;
        last_print_tsc_ = checkpoint_ = now_ = rdtsc();
      } else if (unlikely(FLAGS_s && now_ - last_print_tsc_ >= tsc_hz)) {
        // TODO(barath): Re-add stats printing.
        // PrintStats(last_stats_);
        last_stats_ = stats_;
        last_print_tsc_ = checkpoint_ = now_ = rdtsc();
      }
    }

    ScheduleOnce();
  }
}

void Scheduler::ScheduleOnce() {
  // Schedule.
  TrafficClass *c = Next(now_);

  if (c) {
    ctx.set_current_tsc(now_);  // Tasks see updated tsc.
    ctx.set_current_ns(now_ * ns_per_cycle_);

    // Run.
    LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c);
    struct task_result ret = leaf->RunTasks();

    // Account.
    now_ = rdtsc();
    resource_arr_t usage;
    usage[RESOURCE_COUNT] = 1;
    usage[RESOURCE_CYCLE] = now_ - checkpoint_;
    usage[RESOURCE_PACKET] = ret.packets;
    usage[RESOURCE_BIT] = ret.bits;

    Done(c, usage, now_);
  } else {
    // TODO(barath): Ideally, we wouldn't spin in this case but rather take the
    // fact that Next() returned nullptr as an indication that everything is
    // blocked, so we could wait until something is added that unblocks us.  We
    // currently have no functionality to support such whole-scheduler
    // blocking/unblocking.
    now_ = rdtsc();

    ++stats_.cnt_idle;
    stats_.cycles_idle += (now_ - checkpoint_);
  }

  checkpoint_ = now_;
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

void Scheduler::Done(TrafficClass *c, resource_arr_t usage, uint64_t tsc) {
  // TODO(barath): Re-enable scheduler-wide stats accumulation.
  // accumulate(stats_.usage, usage);

  // Accumulate per-TC stats.
  accumulate(c->stats_.usage, usage);

  // The picked class can never be the root, so we are guaranteed that c has a
  // parent.
  c->parent_->FinishAndAccountTowardsRoot(this, c, usage, tsc);
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

template <typename Func>
void Scheduler::ApplyToAllClasses(Func func) {
  for (const auto &i : TrafficClassBuilder::all_tcs()) {
    if (i.second->Root() == root_) {
      func(i.second);
    }
  }
}

}  // namespace bess
