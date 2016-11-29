#include "scheduler.h"

#include "opts.h"
#include "utils/common.h"
#include "worker.h"

namespace bess {

void Scheduler::ScheduleLoop() {
  // How many rounds to go before we do accounting.
  const uint64_t accounting_mask = 0xff; 
  static_assert(((accounting_mask+1) & accounting_mask) == 0,
                "Accounting mask must be (2^n)-1");

  last_stats_ = stats_;
  last_print_tsc_ = checkpoint_ = now_ = rdtsc();

  // The main scheduling, running, accounting loop.
  for (uint64_t round = 0;; round++) {
    // Periodic check, to mitigate expensive operations.
    if ((round & accounting_mask) == 0) {
      if (unlikely(ctx.is_pause_requested())) {
        if (unlikely(ctx.Block())) {
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

  TrafficClass *c = &root_;
  while (c->policy_ != POLICY_LEAF) {
    switch (c->policy_) {
      case POLICY_PRIORITY: {
        PriorityTrafficClass *pc = static_cast<PriorityTrafficClass *>(c);
        if (unlikely(pc->children_.size() == 0 || pc->first_runnable_ >= pc->children_.size())) {
          return nullptr;
        }
        c = pc->children_[pc->first_runnable_].c_;
        break;
      }

      case POLICY_WEIGHTED_FAIR: {
        WeightedFairTrafficClass *wc = static_cast<WeightedFairTrafficClass *>(c);
        if (unlikely(wc->children_.empty())) {
          return nullptr;
        }
        c = wc->children_.top().c_;
        break;
      }
    
      case POLICY_ROUND_ROBIN: {
        RoundRobinTrafficClass *rrc = static_cast<RoundRobinTrafficClass *>(c);
        if (unlikely(rrc->children_.empty())) {
          return nullptr;
        }
        c = rrc->children_.front();
        break;
      }
    
      case POLICY_RATE_LIMIT: {
        RateLimitTrafficClass *rc = static_cast<RateLimitTrafficClass *>(c);
        c = rc->child_;
        break;
      }

      default: {
        CHECK(false) << "Shouldn't get here.";
        break;
      }
    }
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

  // Upwards from the leaf, skipping the root class.
  while (c->parent_) {
    c->last_tsc_ = tsc;

    TrafficClass *parent = c->parent_;
    switch (parent->policy_) {
      case POLICY_PRIORITY: {
        PriorityTrafficClass *pc = static_cast<PriorityTrafficClass *>(parent);
        if (c->blocked_) {
          // Find the next child that isn't blocked, if there is one.
          while (pc->first_runnable_ < pc->children_.size() &&
                 pc->children_[pc->first_runnable_].c_->blocked_) {
            pc->first_runnable_++;
          }
          if (pc->first_runnable_ >= pc->children_.size()) {
            pc->blocked_ = true;
          }
        }
        break;
      }

      case POLICY_WEIGHTED_FAIR: {
        WeightedFairTrafficClass *wc = static_cast<WeightedFairTrafficClass *>(parent);

        // Update the item's position in the stride scheduling priority queue.
        auto item = wc->children_.top();
        wc->children_.pop();

        DCHECK_EQ(item.c_, c) << "Child that we picked should be at the front of priority queue.";
        if (c->blocked_) {
          wc->blocked_children_.emplace(c, std::move(item));
          if (wc->children_.empty()) {
            wc->blocked_ = true;
          }
        } else {
          uint64_t consumed = usage[wc->resource_];
          item.pass_ += item.stride_ * consumed / QUANTUM;
          wc->children_.emplace(std::move(item));
        }

        break;
      }
    
      case POLICY_ROUND_ROBIN: {
        RoundRobinTrafficClass *rrc = static_cast<RoundRobinTrafficClass *>(parent);
        TrafficClass *front = rrc->children_.front();
        DCHECK_EQ(front, c) << "Child that we picked should be at the front of RR deque.";
        rrc->children_.pop_front();

        if (c->blocked_) {
          rrc->blocked_children_.insert(front);
          if (rrc->children_.empty()) {
            rrc->blocked_ = true;
          }
        } else {
          rrc->children_.push_back(front);
        }
        break;
      }
    
      case POLICY_RATE_LIMIT: {
        // Rate limit policy is special, because it can block and because there
        // is a one-to-one parent-child relationship.
        RateLimitTrafficClass *rc = static_cast<RateLimitTrafficClass *>(parent);
        rc->last_tsc_ = tsc;
        HandleRateLimit(rc, usage[rc->resource_], tsc);

        break;
      }

      default: {
        CHECK(false) << "Shouldn't get here.";
        break;
      }
    }

    c = parent;
  }
}

void Scheduler::HandleRateLimit(RateLimitTrafficClass *rc, uint64_t consumed, uint64_t tsc) {
  uint64_t elapsed_cycles = tsc - rc->last_tsc_;

  consumed = consumed << USAGE_AMPLIFIER_POW;
  uint64_t tokens = rc->tokens_ + rc->limit_ * elapsed_cycles;
  if (tokens < consumed) {
    // Exceeded limit, throttled.
    uint64_t wait_tsc = (consumed - tokens) / rc->limit_;
    rc->tokens_ = 0;
    rc->blocked_ = true;
    rc->stats_.cnt_throttled++;
    rc->throttle_expiration_ = tsc+wait_tsc;

    throttled_cache_.push(rc);
  } else {
    // Still has some tokens, unthrottled.
    rc->tokens_ = std::min(tokens - consumed, rc->max_burst_);
  }
}

void Scheduler::UnblockTowardsRoot(TrafficClass *c, uint64_t tsc) {
  while (c->parent_) {
    c->last_tsc_ = tsc;
    TrafficClass *parent = c->parent_;
    switch (parent->policy_) {
      case POLICY_PRIORITY: {
        PriorityTrafficClass *pc = static_cast<PriorityTrafficClass *>(parent);
        for (size_t i = 0; i < pc->children_.size(); i++) {
          if (pc->children_[i].c_ == c) {
            pc->first_runnable_ = i;
          }
        }
        DCHECK_LT(pc->first_runnable_, pc->children_.size()) << "We should have found the child.";
        if (pc->blocked_) {
          pc->blocked_ = false;
        } else {
          return;
        }
        break;
      }

      case POLICY_WEIGHTED_FAIR: {
        WeightedFairTrafficClass *wc = static_cast<WeightedFairTrafficClass *>(parent);
        auto it = wc->blocked_children_.find(c);
        DCHECK(it == wc->blocked_children_.end()) << "Child wasn't in parent's blocked list.";
        auto childdata = it->second;
        wc->blocked_children_.erase(it);
        childdata.pass_ = 0;

        if (wc->children_.empty()) {
          wc->children_.push(childdata);
          wc->blocked_ = false;
        } else {
          DCHECK(!wc->blocked_) << "Parent shouldn't be blocked.";
          wc->children_.push(childdata);
          return; 
        }

        break;
      }
    
      case POLICY_ROUND_ROBIN: {
        RoundRobinTrafficClass *rrc = static_cast<RoundRobinTrafficClass *>(parent);
        DCHECK(rrc->blocked_children_.count(c)) << "Child wasn't in parent's blocked list.";
        rrc->blocked_children_.erase(c);

        if (rrc->children_.empty()) {
          rrc->children_.push_front(c);
          rrc->blocked_ = false;
        } else {
          DCHECK(!rrc->blocked_) << "Parent shouldn't be blocked.";
          rrc->children_.push_front(c);
          return;
        }

        break;
      }
    
      case POLICY_RATE_LIMIT: {
        // Unblocking a child doesn't affect a parent rate limit class, so we're
        // done.
        return;
      }

      default: {
        CHECK(false) << "Shouldn't get here.";
        break;
      }
    }

    c = c->parent_;
  }
}

void Scheduler::ResumeThrottled(uint64_t tsc) {
  while (!throttled_cache_.empty()) {
    RateLimitTrafficClass *rc = throttled_cache_.top();
    if (rc->throttle_expiration() < tsc) {
      throttled_cache_.pop();
      rc->blocked_ = false;

      // Traverse upward toward root to unblock any blocked parents.
      UnblockTowardsRoot(rc, tsc);
    } else {
      break;
    }
  }
}

}  // namespace bess
