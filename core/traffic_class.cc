#include "traffic_class.h"

#include <algorithm>
#include <cinttypes>
#include <string>

#include "opts.h"
#include "scheduler.h"
#include "task.h"
#include "utils/common.h"
#include "utils/time.h"
#include "worker.h"

namespace bess {

size_t TrafficClass::Size() const {
  size_t ret = 0;
  Traverse(
      []([[maybe_unused]] const bess::TrafficClass* c, void* arg) {
        *reinterpret_cast<size_t*>(arg) += 1;
      },
      static_cast<void*>(&ret));
  return ret;
}

PriorityTrafficClass::~PriorityTrafficClass() {
  for (auto &c : children_) {
    delete c.c_;
  }
  TrafficClassBuilder::Clear(this);
}

bool PriorityTrafficClass::AddChild(TrafficClass *child, priority_t priority) {
  if (child->parent_) {
    return false;
  }

  // Ensure that no child already has the given priority.
  // FIXME: Allow having multiple TCs with the same priority.
  //        (However, who gets scheduled first among them is not guaranteed)
  for (const auto &c : children_) {
    if (c.priority_ == priority) {
      return false;
    }
  }

  ChildData d{priority, child};
  InsertSorted(children_, d);
  child->parent_ = this;

  UnblockTowardsRoot(rdtsc());

  return true;
}

bool PriorityTrafficClass::RemoveChild(TrafficClass *child) {
  if (child->parent_ != this) {
    return false;
  }

  for (auto it = children_.begin(); it != children_.end(); it++) {
    if (it->c_ == child) {
      children_.erase(it);
      child->parent_ = nullptr;
      if (static_cast<size_t>(it - children_.begin()) < first_runnable_) {
        first_runnable_--;
      }
      BlockTowardsRoot();

      return true;
    }
  }

  return false;
}

TrafficClass *PriorityTrafficClass::PickNextChild() {
  return children_[first_runnable_].c_;
}

void PriorityTrafficClass::UnblockTowardsRoot(uint64_t tsc) {
  size_t num_children = children_.size();
  for (first_runnable_ = 0; first_runnable_ < num_children; ++first_runnable_) {
    if (!children_[first_runnable_].c_->blocked_) {
      break;
    }
  }
  TrafficClass::UnblockTowardsRootSetBlocked(tsc,
                                             first_runnable_ >= num_children);
}

void PriorityTrafficClass::BlockTowardsRoot() {
  size_t num_children = children_.size();
  while (first_runnable_ < num_children &&
         children_[first_runnable_].c_->blocked_) {
    ++first_runnable_;
  }
  TrafficClass::BlockTowardsRootSetBlocked(first_runnable_ == num_children);
}

void PriorityTrafficClass::FinishAndAccountTowardsRoot(SchedThrottledCache *thr,
                                                       TrafficClass *child,
                                                       resource_arr_t usage,
                                                       uint64_t tsc) {
  ACCUMULATE(stats_.usage, usage);

  if (child->blocked_) {
    // Find the next child that isn't blocked, if there is one.
    size_t num_children = children_.size();
    while (first_runnable_ < num_children &&
           children_[first_runnable_].c_->blocked_) {
      ++first_runnable_;
    }
    blocked_ = (first_runnable_ == num_children);
  }
  if (!parent_) {
    return;
  }
  parent_->FinishAndAccountTowardsRoot(thr, this, usage, tsc);
}

void PriorityTrafficClass::Traverse(TraverseTcFn f, void *arg) const {
  f(this, arg);
  for (const auto &child : children_) {
    child.c_->Traverse(f, arg);
  }
}

WeightedFairTrafficClass::~WeightedFairTrafficClass() {
  while (!children_.empty()) {
    delete children_.top().c_;
    children_.pop();
  }
  for (auto &c : blocked_children_) {
    delete c.c_;
  }
  TrafficClassBuilder::Clear(this);
}

bool WeightedFairTrafficClass::AddChild(TrafficClass *child,
                                        resource_share_t share) {
  if (child->parent_) {
    return false;
  }

  int64_t pass = 0;
  if (!children_.empty()) {
    pass = children_.top().pass_;
  }

  child->parent_ = this;
  WeightedFairTrafficClass::ChildData child_data{STRIDE1 / share, pass, child};
  if (child->blocked_) {
    blocked_children_.push_back(child_data);
  } else {
    children_.push(child_data);
    UnblockTowardsRoot(rdtsc());
  }

  return true;
}

bool WeightedFairTrafficClass::RemoveChild(TrafficClass *child) {
  if (child->parent_ != this) {
    return false;
  }

  for (auto it = blocked_children_.begin();
       it != blocked_children_.end(); it++) {
    if (it->c_ == child) {
      blocked_children_.erase(it);
      child->parent_ = nullptr;
      return true;
    }
  }

  bool ret = children_.delete_single_element([=](const ChildData &x) {
    return x.c_ == child;
  });
  if (ret) {
    child->parent_ = nullptr;
    BlockTowardsRoot();
    return true;
  }

  return false;
}

TrafficClass *WeightedFairTrafficClass::PickNextChild() {
  return children_.top().c_;
}

void WeightedFairTrafficClass::UnblockTowardsRoot(uint64_t tsc) {
  // TODO(barath): Optimize this unblocking behavior.
  for (auto it = blocked_children_.begin(); it != blocked_children_.end();) {
    if (!it->c_->blocked_) {
      it->pass_ = 0;
      children_.push(*it);
      blocked_children_.erase(it++);
    } else {
      ++it;
    }
  }

  TrafficClass::UnblockTowardsRootSetBlocked(tsc, children_.empty());
}

void WeightedFairTrafficClass::BlockTowardsRoot() {
  children_.delete_single_element([&](const ChildData &x) {
    if (x.c_->blocked_) {
      blocked_children_.push_back(x);
      return true;
    }
    return false;
  });

  TrafficClass::BlockTowardsRootSetBlocked(children_.empty());
}

void WeightedFairTrafficClass::FinishAndAccountTowardsRoot(
                                                       SchedThrottledCache *thr,
                                                       TrafficClass *child,
                                                       resource_arr_t usage,
                                                       uint64_t tsc) {
  ACCUMULATE(stats_.usage, usage);

  // DCHECK_EQ(item.c_, child) << "Child that we picked should be at the front
  // of priority queue.";
  if (child->blocked_) {
    auto item = children_.top();
    children_.pop();
    blocked_children_.emplace_back(std::move(item));
    blocked_ = children_.empty();
  } else {
    auto &item = children_.mutable_top();
    uint64_t consumed = usage[resource_];
    item.pass_ += item.stride_ * consumed / QUANTUM;
    children_.decrease_key_top();
  }

  if (!parent_) {
    return;
  }
  parent_->FinishAndAccountTowardsRoot(thr, this, usage, tsc);
}

void WeightedFairTrafficClass::Traverse(TraverseTcFn f, void *arg) const {
  f(this, arg);
  for (const auto &child : children_.container()) {
    child.c_->Traverse(f, arg);
  }
  for (const auto &child : blocked_children_) {
    child.c_->Traverse(f, arg);
  }
}

RoundRobinTrafficClass::~RoundRobinTrafficClass() {
  for (TrafficClass *c : children_) {
    delete c;
  }
  for (TrafficClass *c : blocked_children_) {
    delete c;
  }
  TrafficClassBuilder::Clear(this);
}

bool RoundRobinTrafficClass::AddChild(TrafficClass *child) {
  if (child->parent_) {
    return false;
  }
  child->parent_ = this;

  if (child->blocked_) {
    blocked_children_.push_back(child);
  } else {
    children_.push_back(child);
  }

  UnblockTowardsRoot(rdtsc());

  return true;
}

bool RoundRobinTrafficClass::RemoveChild(TrafficClass *child) {
  if (child->parent_ != this) {
    return false;
  }

  for (auto it = blocked_children_.begin();
       it != blocked_children_.end(); it++) {
    if (*it == child) {
      blocked_children_.erase(it);
      child->parent_ = nullptr;
      return true;
    }
  }

  for (auto it = children_.begin(); it != children_.end(); it++) {
    if (*it == child) {
      if (static_cast<size_t>(it - children_.begin()) < next_child_) {
        next_child_--;
      }
      children_.erase(it);
      // Wrap around for round robin.
      if (next_child_ >= children_.size()) {
        next_child_ = 0;
      }
      child->parent_ = nullptr;
      BlockTowardsRoot();
      return true;
    }
  }

  return false;
}

TrafficClass *RoundRobinTrafficClass::PickNextChild() {
  return children_[next_child_];
}

void RoundRobinTrafficClass::UnblockTowardsRoot(uint64_t tsc) {
  // TODO(barath): Optimize this unblocking behavior.
  for (auto it = blocked_children_.begin(); it != blocked_children_.end();) {
    if (!(*it)->blocked_) {
      children_.push_back(*it);
      it = blocked_children_.erase(it);
    } else {
      ++it;
    }
  }

  TrafficClass::UnblockTowardsRootSetBlocked(tsc, children_.empty());
}

void RoundRobinTrafficClass::BlockTowardsRoot() {
  for (auto it = children_.begin(); it != children_.end();) {
    if ((*it)->blocked_) {
      if (static_cast<size_t>(it - children_.begin()) < next_child_) {
        next_child_--;
      }
      blocked_children_.push_back(*it);
      it = children_.erase(it);
      // Wrap around for round robin.
      if (next_child_ >= children_.size()) {
        next_child_ = 0;
      }
    } else {
      ++it;
    }
  }
  TrafficClass::BlockTowardsRootSetBlocked(children_.empty());
}

void RoundRobinTrafficClass::FinishAndAccountTowardsRoot(
                                                     SchedThrottledCache *thr,
                                                     TrafficClass *child,
                                                     resource_arr_t usage,
                                                     uint64_t tsc) {
  ACCUMULATE(stats_.usage, usage);
  if (child->blocked_) {
    children_.erase(children_.begin() + next_child_);
    blocked_children_.push_back(child);
    blocked_ = children_.empty();
  } else {
    ++next_child_;
  }

  // Wrap around for round robin.
  if (next_child_ >= children_.size()) {
    next_child_ = 0;
  }

  if (!parent_) {
    return;
  }
  parent_->FinishAndAccountTowardsRoot(thr, this, usage, tsc);
}

void RoundRobinTrafficClass::Traverse(TraverseTcFn f, void *arg) const {
  f(this, arg);
  for (const auto &child : children_) {
    child->Traverse(f, arg);
  }
  for (const auto &child : blocked_children_) {
    child->Traverse(f, arg);
  }
}

RateLimitTrafficClass::~RateLimitTrafficClass() {
  // TODO(barath): Ensure that when this destructor is called this instance is
  // also cleared out of the throttled_cache_ in Scheduler if it is present
  // there.
  delete child_;
  TrafficClassBuilder::Clear(this);
}

bool RateLimitTrafficClass::AddChild(TrafficClass *child) {
  if (child->parent_ || child_ != nullptr) {
    return false;
  }

  child_ = child;
  child->parent_ = this;

  UnblockTowardsRoot(rdtsc());

  return true;
}

bool RateLimitTrafficClass::RemoveChild(TrafficClass *child) {
  if (child->parent_ != this || child != child_) {
    return false;
  }

  child_->parent_ = nullptr;
  child_ = nullptr;

  BlockTowardsRoot();

  return true;
}

TrafficClass *RateLimitTrafficClass::PickNextChild() {
  return child_;
}

void RateLimitTrafficClass::UnblockTowardsRoot(uint64_t tsc) {
  last_tsc_ = tsc;

  bool blocked = throttle_expiration_ || !child_ || child_->blocked_;
  TrafficClass::UnblockTowardsRootSetBlocked(tsc, blocked);
}

void RateLimitTrafficClass::BlockTowardsRoot() {
  bool blocked = !child_ || child_->blocked_;
  TrafficClass::BlockTowardsRootSetBlocked(blocked);
}

void RateLimitTrafficClass::FinishAndAccountTowardsRoot(
                                                    SchedThrottledCache *thr,
                                                    TrafficClass *child,
                                                    resource_arr_t usage,
                                                    uint64_t tsc) {
  ACCUMULATE(stats_.usage, usage);
  uint64_t elapsed_cycles = tsc - last_tsc_;
  last_tsc_ = tsc;

  uint64_t tokens = tokens_ + limit_ * elapsed_cycles;
  uint64_t consumed = usage[resource_] << USAGE_AMPLIFIER_POW;
  if (tokens < consumed) {
    // Exceeded limit, throttled.
    tokens_ = 0;
    blocked_ = true;
    ++stats_.cnt_throttled;

    uint64_t wait_tsc = (consumed - tokens) / limit_;
    throttle_expiration_ = tsc + wait_tsc;
    thr->AddThrottled(this);
  } else {
    // Still has some tokens, unthrottled.
    tokens_ = std::min(tokens - consumed, max_burst_);
  }

  // Can still become blocked if the child was blocked, even if we haven't hit
  // the rate limit.
  blocked_ |= child->blocked_;

  if (!parent_) {
    return;
  }
  parent_->FinishAndAccountTowardsRoot(thr, this, usage, tsc);
}

void RateLimitTrafficClass::Traverse(TraverseTcFn f, void *arg) const {
  f(this, arg);
  if (child_) {
    child_->Traverse(f, arg);
  }
}

std::unordered_map<std::string, TrafficClass *> TrafficClassBuilder::all_tcs_;

bool TrafficClassBuilder::ClearAll() {
  all_tcs_.clear();
  return true;
}

bool TrafficClassBuilder::Clear(TrafficClass *c) {
  bool ret = all_tcs_.erase(c->name());
  return ret;
}

}  // namespace bess
