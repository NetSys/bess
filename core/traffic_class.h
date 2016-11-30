#ifndef BESS_TC_H_
#define BESS_TC_H_

#include <deque>
#include <list>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "task.h"
#include "utils/common.h"
#include "utils/extended_priority_queue.h"
#include "utils/simd.h"
#include "utils/time.h"

namespace bess {

#define SCHED_DEBUG 0

// A large default priority.
#define DEFAULT_PRIORITY 0xFFFFFFFFu

#define MAX_LIMIT_POW 36
#define USAGE_AMPLIFIER_POW 32

/* share is defined relatively, so 1024 should be large enough */
#define MAX_SHARE (1 << 10)
#define STRIDE1 (1 << 20)

/* this doesn't mean anything, other than avoiding int64 overflow */
#define QUANTUM (1 << 10)

// Resource types that can be accounted for.
enum resource_t {
  RESOURCE_COUNT = 0, // Count of how many times scheduled
  RESOURCE_CYCLE,     // CPU cycles
  RESOURCE_PACKET,    // Packets set
  RESOURCE_BIT,       // Bits sent
  NUM_RESOURCES,      // Sentinel. Do not use.
};

// An array of counters for all resource types.
typedef uint64_t resource_arr_t[NUM_RESOURCES] __ymm_aligned;

// The priority of a traffic class.
typedef uint32_t priority_t;

// The amount of a resource allocated to a class.
typedef int32_t resource_share_t;

struct tc_stats {
  resource_arr_t usage;
  uint64_t cnt_throttled;
};

class Scheduler;
class TrafficClassBuilder;
class PriorityTrafficClass;
class WeightedFairTrafficClass;
class RoundRobinTrafficClass;
class RateLimitTrafficClass;
class LeafTrafficClass;

enum TrafficPolicy {
  POLICY_PRIORITY = 0,
  POLICY_WEIGHTED_FAIR,
  POLICY_ROUND_ROBIN,
  POLICY_RATE_LIMIT,
  POLICY_LEAF,
  NUM_POLICIES,  // sentinel
};

// A TrafficClass represents a hierarchy of TrafficClasses which contain
// schedulable task units.
class TrafficClass {
 public:
  virtual ~TrafficClass() {
    // TODO(barath): Clean up pointers to parents, children, etc.
  }

  inline TrafficClass *parent() const { return parent_; }

  const std::string &name() const { return name_; }

  inline const struct tc_stats &stats() const { return stats_; }

  inline bool blocked() const { return blocked_; }

  // For testing / debugging only.
  void set_blocked(bool blocked) { blocked_ = blocked; }

  inline TrafficPolicy policy() const { return policy_; }

 protected:
  friend PriorityTrafficClass;
  friend WeightedFairTrafficClass;
  friend RoundRobinTrafficClass;
  friend RateLimitTrafficClass;
  friend LeafTrafficClass;

  TrafficClass(const std::string &name, const TrafficPolicy &policy)
    : parent_(),
      name_(name),
      last_tsc_(),
      stats_(),
      blocked_(true),
      policy_(policy) {}

  // Sets blocked status to nowblocked and recurses towards root if our blocked
  // status changed.
  inline void UnblockTowardsRootSetBlocked(uint64_t tsc, bool nowblocked) {
    bool became_unblocked = !nowblocked && blocked_;
    blocked_ = nowblocked;

    if (!parent_ || !became_unblocked) {
      return;
    }

    parent_->UnblockTowardsRoot(tsc);
  }

  // Returns the next schedulable child of this traffic class.
  virtual TrafficClass *PickNextChild() = 0;

  // Starts from the current node and attempts to recursively unblock (if
  // eligible) all nodes from this node to the root.
  virtual void UnblockTowardsRoot(uint64_t tsc) = 0;

  // Starts from the current node and accounts for the usage of the given child
  // after execution and finishes any data structure reorganization required
  // after execution has finished.  The scheduler is the scheduler that owns
  // these classes.
  virtual void FinishAndAccountTowardsRoot(
      Scheduler *sched,
      TrafficClass *child,
      resource_arr_t usage,
      uint64_t tsc) = 0;

  // Parent of this class; nullptr for root.
  TrafficClass *parent_;

  // The name given to this class.
  std::string name_;

  // Last time this class was scheduled.
  uint64_t last_tsc_;

  struct tc_stats stats_;

 private:
  friend Scheduler;
  friend TrafficClassBuilder;

  bool blocked_;

  TrafficPolicy policy_;

  DISALLOW_COPY_AND_ASSIGN(TrafficClass);
};

class PriorityTrafficClass final : public TrafficClass {
 public:
  PriorityTrafficClass(const std::string &name)
    : TrafficClass(name, POLICY_PRIORITY),
      first_runnable_(0),
      children_() {}

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child, priority_t priority);

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;

  void FinishAndAccountTowardsRoot(
      Scheduler *sched,
      TrafficClass *child,
      resource_arr_t usage,
      uint64_t tsc) override;

 private:
  friend Scheduler;

  struct ChildData {
    inline bool operator<(const ChildData &right) const {
      return priority_ < right.priority_;
    }

    priority_t priority_;
    TrafficClass *c_;
  };

  size_t first_runnable_;  // Index of first member of children_ that is runnable.
  std::vector<ChildData> children_;
};

class WeightedFairTrafficClass final : public TrafficClass {
 public:
  WeightedFairTrafficClass(const std::string &name, resource_t resource)
    : TrafficClass(name, POLICY_WEIGHTED_FAIR),
      resource_(resource),
      children_(),
      blocked_children_() {}

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child, resource_share_t share);

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;

  void FinishAndAccountTowardsRoot(
      Scheduler *sched,
      TrafficClass *child,
      resource_arr_t usage,
      uint64_t tsc) override;

 private:
  friend Scheduler;

  // The resource that we are sharing.
  resource_t resource_;

  struct ChildData {
    inline bool operator<(const ChildData &right) const {
      // Reversed so that priority_queue is a min priority queue.
      return right.pass_ < pass_;
    }

    int64_t stride_;
    int64_t pass_;

    TrafficClass *c_;
  };

  extended_priority_queue<ChildData> children_;
  std::list<ChildData> blocked_children_;
};

class RoundRobinTrafficClass final : public TrafficClass {
 public:
  RoundRobinTrafficClass(const std::string &name)
    : TrafficClass(name, POLICY_ROUND_ROBIN),
      children_(),
      blocked_children_() {}

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child);

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;

  void FinishAndAccountTowardsRoot(
      Scheduler *sched,
      TrafficClass *child,
      resource_arr_t usage,
      uint64_t tsc) override;

 private:
  friend Scheduler;

  std::deque<TrafficClass *> children_;
  std::list<TrafficClass *> blocked_children_;
};

// Performs rate limiting on a single child class (which could implement some
// other policy with many children).  Rate limit policy is special, because it
// can block and because there is a one-to-one parent-child relationship.
class RateLimitTrafficClass final : public TrafficClass {
 public:
  RateLimitTrafficClass(const std::string &name, resource_t resource,
                        uint64_t limit, uint64_t max_burst)
    : TrafficClass(name, POLICY_RATE_LIMIT),
      resource_(resource),
      limit_(),
      max_burst_(),
      tokens_(),
      throttle_expiration_(),
      child_() {
    limit_ = (limit << (USAGE_AMPLIFIER_POW - 4)) / (tsc_hz >> 4);
    if (limit_) {
      max_burst_ = (max_burst << (USAGE_AMPLIFIER_POW - 4)) / (tsc_hz >> 4);
    }
  }

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child);

  inline uint64_t throttle_expiration() const { return throttle_expiration_; }

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;

  void FinishAndAccountTowardsRoot(
      Scheduler *sched,
      TrafficClass *child,
      resource_arr_t usage,
      uint64_t tsc) override;

 private:
  friend Scheduler;

  // The resource that we are limiting.
  resource_t resource_;

  // For per-resource token buckets:
  // 1 work unit = 2 ^ USAGE_AMPLIFIER_POW resource usage.
  // (for better precision without using floating point numbers)
  //
  // prof->limit < 2^36 (~64 Tbps)
  // 2^24 < tsc_hz < 2^34 (16 Mhz - 16 GHz)
  // tb->limit < 2^36
  uint64_t limit_;      // In bits/pkts/cycles per sec. (0 if unlimited).
  uint64_t max_burst_;  // In work units.
  uint64_t tokens_;     // In work units.

  uint64_t throttle_expiration_;

  TrafficClass *child_;
};

class LeafTrafficClass final : public TrafficClass {
 public:
  LeafTrafficClass(const std::string &name)
    : TrafficClass(name, POLICY_LEAF),
      task_index_(),
      tasks_() {}

  // Executes tasks for a leaf TrafficClass.
  inline struct task_result RunTasks() {
    size_t start = task_index_;
    while (task_index_ < tasks_.size()) {
      struct task_result ret = tasks_[task_index_++]->Scheduled();
      if (ret.packets) {
        return ret;
      }
    }

    // Slight code duplication in order to avoid a loop with a mod operation.
    task_index_ = 0;
    while (task_index_ < start) {
      struct task_result ret = tasks_[task_index_++]->Scheduled();
      if (ret.packets) {
        return ret;
      }
    }

    return (struct task_result){.packets = 0, .bits = 0};
  }

  void AddTask(Task *t);

  // Removes the task from this class; returns true upon success.
  bool RemoveTask(Task *t);

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot([[maybe_unused]] uint64_t tsc) override {
    // TODO(barath): Change this so that we actually look to see if we have
    // tasks to execute and unblock if so.
    return;
  }

  void FinishAndAccountTowardsRoot(
      [[maybe_unused]] Scheduler *sched,
      [[maybe_unused]] TrafficClass *child,
      [[maybe_unused]] resource_arr_t usage,
      uint64_t tsc) override {
    last_tsc_ = tsc;
    // TODO(barath): Consider if there is any accounting to be done for leaf
    // classes.
    return;
  }

 private:
  friend Scheduler;
  friend TrafficClassBuilder;

  // The tasks of this class.  Always empty if this is a non-leaf class.
  // task_index_ keeps track of the next task to run.
  size_t task_index_;
  std::vector<Task *> tasks_;
};

class TrafficClassBuilder {
 public:
  template<typename T, typename... Args>
  static T *CreateTrafficClass(const std::string &name, Args... args) {
    if (all_tcs_.count(name)) {
      return nullptr;
    }

    T *c = new T(name, args...);
    all_tcs_.emplace(name, c);
    return c;
  }

  // Attempts to destroy all classes.  Returns true upon success.
  static bool DestroyAll();

  static inline const std::unordered_map<std::string, TrafficClass *> &all_tcs() {
    return all_tcs_;
  }

 private:
  static std::unordered_map<std::string, TrafficClass *> all_tcs_;
};

}  // namespace bess

#endif  // BESS_TC_H_
