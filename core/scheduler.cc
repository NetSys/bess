#include "scheduler.h"

#include "opts.h"
#include "traffic_class.h"
#include "utils/common.h"
#include "worker.h"

namespace bess {

const std::string Scheduler::kRootClassNamePrefix = "root_";
const std::string Scheduler::kDefaultLeafClassNamePrefix = "defaultleaf_";
const TrafficPolicy Scheduler::kDefaultRootPolicy = POLICY_PRIORITY;

Scheduler::Scheduler(int worker_id)
    : Scheduler(worker_id, kDefaultRootPolicy, NUM_RESOURCES, 0, 0) {
  const priority_t kDefaultPriority = 10;
  std::string name = kDefaultLeafClassNamePrefix + std::to_string(worker_id);
  default_leaf_class_ =
      TrafficClassBuilder::CreateTrafficClass<LeafTrafficClass>(name);
  static_cast<PriorityTrafficClass *>(root_)->AddChild(default_leaf_class_, kDefaultPriority);
}

Scheduler::Scheduler(int worker_id,
                     TrafficPolicy root_policy,
                     [[maybe_unused]] resource_t resource,
                     [[maybe_unused]] uint64_t limit,
                     [[maybe_unused]] uint64_t max_burst)
    : root_(),
    default_leaf_class_(),
    have_throttled_(),
    throttled_cache_(ThrottledComp()),
    stats_(),
    last_stats_(),
    last_print_tsc_(),
    checkpoint_(),
    now_(),
    ns_per_cycle_(1e9 / tsc_hz) {
  std::string root_name = kRootClassNamePrefix + std::to_string(worker_id);
  switch (root_policy) {
    case POLICY_PRIORITY:
      root_ = new PriorityTrafficClass(root_name);
      break;
    case POLICY_WEIGHTED_FAIR:
      CHECK_LT(resource, NUM_RESOURCES);
      CHECK_GE(resource, 0);
      root_ = new WeightedFairTrafficClass(root_name, resource);
      break;
    case POLICY_ROUND_ROBIN:
      root_ = new RoundRobinTrafficClass(root_name);
      break;
    case POLICY_RATE_LIMIT:
      CHECK_LT(resource, NUM_RESOURCES);
      CHECK_GE(resource, 0);
      root_ = new RateLimitTrafficClass(root_name, resource, limit, max_burst);
      break;
    default:
      CHECK(false) << "Policy " << root_policy << " invalid.";
      break;
  }
}

void Scheduler::ScheduleLoop() {
  // How many rounds to go before we do accounting.
  const uint64_t accounting_mask = 0xffff; 
  static_assert(((accounting_mask+1) & accounting_mask) == 0,
                "Accounting mask must be (2^n)-1");

  last_stats_ = stats_;
  last_print_tsc_ = checkpoint_ = now_ = rdtsc();

  // The main scheduling, running, accounting loop.
  for (uint64_t round = 0;; round++) {
    // Periodic check, to mitigate expensive operations.
    if ((round & accounting_mask) == 0) {
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
  TrafficClass *c = Next();

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
    now_ = rdtsc();

    stats_.cnt_idle++;
    stats_.cycles_idle += (now_ - checkpoint_);
  }

  checkpoint_ = now_;
}

TrafficClass *Scheduler::Next() {
  // Before we select the next class to run, resume any classes that were
  // throttled whose throttle time has expired so that they are available.
  ResumeThrottled(now_);

  TrafficClass *c = root_;
  while (c->policy_ != POLICY_LEAF) {
    c = c->PickNextChild();
  }

  return c;
}

/* acc += x */
static inline void accumulate(resource_arr_t acc, resource_arr_t x) {
  uint64_t *p1 = acc;
  uint64_t *p2 = x;

  for (int i = 0; i < NUM_RESOURCES; i++) {
    p1[i] += p2[i];
  }
}

void Scheduler::Done(TrafficClass *c, resource_arr_t usage, uint64_t tsc) {
  accumulate(stats_.usage, usage);

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
    if (rc->throttle_expiration() < tsc) {
      throttled_cache_.pop();
      rc->blocked_ = false;

      // Traverse upward toward root to unblock any blocked parents.
      rc->UnblockTowardsRoot(tsc);
    } else {
      break;
    }
  }
}

}  // namespace bess
