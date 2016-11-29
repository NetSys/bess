#include "traffic_class.h"

#include <algorithm>
#include <cinttypes>
#include <string>

#include "opts.h"
#include "task.h"
#include "utils/common.h"
#include "utils/time.h"
#include "worker.h"

namespace bess {

std::unordered_map<std::string, TrafficClass *> TrafficClassBuilder::all_tcs_;

bool TrafficClassBuilder::DestroyAll() {
  for (const auto &it : all_tcs_) {
    TrafficClass *c = it.second;

    if (c->policy_ == POLICY_LEAF) {
      LeafTrafficClass *l = static_cast<LeafTrafficClass *>(c);
      if (!l->tasks_.empty()) {
        return false;
      }
    }
  }

  for (const auto &it : all_tcs_) {
    delete it.second;
  }

  all_tcs_.clear();
  return true;
}

bool PriorityTrafficClass::AddChild(TrafficClass *child, priority_t priority) {
  if (child->parent_) {
    return false;
  }

  ChildData d{priority, child};
  InsertSorted(children_, d);
  child->parent_ = this;

  for (first_runnable_ = 0; first_runnable_ < children_.size(); ++first_runnable_) {
    if (!children_[first_runnable_].c_->blocked_) {
      break;
    }
  }
  blocked_ = (first_runnable_ >= children_.size());

  return true;
}

bool WeightedFairTrafficClass::AddChild(TrafficClass *child, resource_share_t share) {
  if (child->parent_) {
    return false;
  }

  int64_t pass = 0;
  if (!children_.empty()) {
    pass = children_.top().pass_;
  }

  children_.emplace(WeightedFairTrafficClass::ChildData{STRIDE1 / share, pass, child});
  child->parent_ = this;

  blocked_ = children_.empty();

  return true;
}

bool RoundRobinTrafficClass::AddChild(TrafficClass *child) {
  if (child->parent_) {
    return false;
  }
  children_.push_front(child);

  // TODO(barath): Check blocked status, change if needed.

  child->parent_ = this;
  return true;
}

bool RateLimitTrafficClass::AddChild(TrafficClass *child) {
  if (child->parent_ || child_ != nullptr) {
    return false;
  }

  child_ = child;
  // TODO(barath): Check blocked status, change if needed.

  child->parent_ = this;
  return true;
}

void LeafTrafficClass::AddTask(Task *t) {
  tasks_.push_back(t);
}

bool LeafTrafficClass::RemoveTask(Task *t) {
  auto it = std::find(tasks_.begin(), tasks_.end(), t);
  if (it != tasks_.end()) {
    tasks_.erase(it);
    return true;
  }
  return false;
}


}  // namespace bess
