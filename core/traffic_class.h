// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_TRAFFIC_CLASS_H_
#define BESS_TRAFFIC_CLASS_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "utils/common.h"
#include "utils/extended_priority_queue.h"
#include "utils/simd.h"
#include "utils/time.h"

using bess::utils::extended_priority_queue;

namespace bess {

// A large default priority.
#define DEFAULT_PRIORITY 0xFFFFFFFFu

// Share is defined relatively, so 1024 should be large enough
#define STRIDE1 (1 << 20)

// This doesn't mean anything, other than avoiding int64 overflow
#define QUANTUM (1 << 10)

// Resource types that can be accounted for.
enum resource_t {
  RESOURCE_COUNT = 0,  // Count of how many times scheduled
  RESOURCE_CYCLE,      // CPU cycles
  RESOURCE_PACKET,     // Packets set
  RESOURCE_BIT,        // Bits sent
  NUM_RESOURCES,       // Sentinel. Also used to indicate "no resource".
};

// An array of counters for all resource types.
typedef uint64_t resource_arr_t[NUM_RESOURCES];

// The priority of a traffic class.
typedef uint32_t priority_t;

// The amount of a resource allocated to a class.
typedef int32_t resource_share_t;

struct tc_stats {
  resource_arr_t usage;
  uint64_t cnt_throttled;
};

template <typename CallableTask>
class Scheduler;
class SchedWakeupQueue;
class TrafficClassBuilder;
class PriorityTrafficClass;
class WeightedFairTrafficClass;
class RoundRobinTrafficClass;
class RateLimitTrafficClass;
template <typename CallableTask>
class LeafTrafficClass;
class TrafficClass;

enum TrafficPolicy {
  POLICY_PRIORITY = 0,
  POLICY_WEIGHTED_FAIR,
  POLICY_ROUND_ROBIN,
  POLICY_RATE_LIMIT,
  POLICY_LEAF,
  NUM_POLICIES,  // sentinel
};

namespace traffic_class_initializer_types {
enum PriorityFakeType {
  PRIORITY = 0,
};
enum WeightedFairFakeType {
  WEIGHTED_FAIR = 0,
};
enum RoundRobinFakeType {
  ROUND_ROBIN = 0,
};
enum RateLimitFakeType {
  RATE_LIMIT = 0,
};
enum LeafFakeType {
  LEAF = 0,
};

}  // namespace traffic_class_initializer_types

using namespace traffic_class_initializer_types;

const std::string TrafficPolicyName[NUM_POLICIES] = {
    "priority", "weighted_fair", "round_robin", "rate_limit", "leaf"};

const std::unordered_map<std::string, enum resource_t> ResourceMap = {
    {"count", RESOURCE_COUNT},
    {"cycle", RESOURCE_CYCLE},
    {"packet", RESOURCE_PACKET},
    {"bit", RESOURCE_BIT}};

const std::unordered_map<int, std::string> ResourceName = {
    {RESOURCE_COUNT, "count"},
    {RESOURCE_CYCLE, "cycle"},
    {RESOURCE_PACKET, "packet"},
    {RESOURCE_BIT, "bit"}};

/* acc += x */
#define ACCUMULATE(acc, x)                                \
  {                                                       \
    uint64_t *p1 = acc;                                   \
    uint64_t *p2 = x;                                     \
    for (int index = 0; index < NUM_RESOURCES; ++index) { \
      p1[index] += p2[index];                             \
    }                                                     \
  }

class TCChildArgs {
 public:
  TCChildArgs(TrafficClass *child)
      : parent_type_(NUM_POLICIES), child_(child) {}
  TrafficClass *child() { return child_; }
  TrafficPolicy parent_type() { return parent_type_; }

 protected:
  TCChildArgs(TrafficPolicy parent_type, TrafficClass *child)
      : parent_type_(parent_type), child_(child) {}

 private:
  const TrafficPolicy parent_type_;
  TrafficClass *child_;
};

// A TrafficClass represents a hierarchy of TrafficClasses which contain
// schedulable task units.
class TrafficClass {
 public:
  virtual ~TrafficClass() {}

  // Returns the number of TCs in the TC subtree rooted at this, including
  // this TC.
  size_t Size() const;

  virtual std::vector<TrafficClass *> Children() const = 0;

  // Returns the root of the tree this class belongs to.
  // Expensive in that it is recursive, so do not call from
  // performance-sensitive code.
  const TrafficClass *Root() const { return parent_ ? parent_->Root() : this; }
  TrafficClass *Root() { return parent_ ? parent_->Root() : this; }

  // Returns its worker ID, or -1 (kAnyWorker) if not belongs to any worker yet
  int WorkerId() const;

  // Returns true if 'child' was removed successfully, in which case
  // the caller owns it. Therefore, after a successful call, 'child'
  // must be destroyed or attached to another tree.
  virtual bool RemoveChild(TrafficClass *child) = 0;

  // Starts from the current node and accounts for the usage of the given child
  // after execution and finishes any data structure reorganization required
  // after execution has finished.
  virtual void FinishAndAccountTowardsRoot(SchedWakeupQueue *wakeup_queue,
                                           TrafficClass *child,
                                           resource_arr_t usage,
                                           uint64_t tsc) = 0;

  TrafficClass *parent() const { return parent_; }

  const std::string &name() const { return name_; }

  const struct tc_stats &stats() const { return stats_; }

  uint64_t wakeup_time() const { return wakeup_time_; }

  bool blocked() const { return blocked_; }

  TrafficPolicy policy() const { return policy_; }

 protected:
  friend PriorityTrafficClass;
  friend WeightedFairTrafficClass;
  friend RoundRobinTrafficClass;
  friend RateLimitTrafficClass;
  template <typename CallableTask>
  friend class LeafTrafficClass;

  TrafficClass(const std::string &name, const TrafficPolicy &policy,
               bool blocked = true)
      : parent_(),
        name_(name),
        stats_(),
        wakeup_time_(),
        blocked_(blocked),
        policy_(policy) {}

  // Sets blocked status to nowblocked and recurses towards root by signaling
  // the parent if status became unblocked.
  void UnblockTowardsRootSetBlocked(uint64_t tsc, bool nowblocked) {
    bool became_unblocked = !nowblocked && blocked_;
    blocked_ = nowblocked;

    if (!parent_ || !became_unblocked) {
      return;
    }

    parent_->UnblockTowardsRoot(tsc);
  }

  // Sets blocked status to nowblocked and recurses towards root by signaling
  // the parent if status became blocked.
  void BlockTowardsRootSetBlocked(bool nowblocked) {
    bool became_blocked = nowblocked && !blocked_;
    blocked_ = nowblocked;

    if (!parent_ || !became_blocked) {
      return;
    }

    parent_->BlockTowardsRoot();
  }

  // Returns the next schedulable child of this traffic class.
  virtual TrafficClass *PickNextChild() = 0;

  // Starts from the current node and attempts to recursively unblock (if
  // eligible) all nodes from this node to the root.
  virtual void UnblockTowardsRoot(uint64_t tsc) = 0;

  // Starts from the current node and attempts to recursively block (if
  // eligible) all nodes from this node to the root.
  virtual void BlockTowardsRoot() = 0;

  // Parent of this class; nullptr for root.
  TrafficClass *parent_;

  // The name given to this class.
  const std::string name_;

  struct tc_stats stats_;

  // The tsc time that this should be woken up by the scheduler.
  uint64_t wakeup_time_;

 private:
  template <typename CallableTask>
  friend class Scheduler;
  template <typename CallableTask>
  friend class DefaultScheduler;
  template <typename CallableTask>
  friend class ExperimentalScheduler;

  bool blocked_;

  const TrafficPolicy policy_;

  DISALLOW_COPY_AND_ASSIGN(TrafficClass);
};

class PriorityTrafficClass final : public TrafficClass {
 public:
  struct ChildData {
    bool operator<(const ChildData &right) const {
      return priority_ < right.priority_;
    }

    priority_t priority_;
    TrafficClass *c_;
  };

  explicit PriorityTrafficClass(const std::string &name)
      : TrafficClass(name, POLICY_PRIORITY), first_runnable_(0), children_() {}

  ~PriorityTrafficClass();

  std::vector<TrafficClass *> Children() const override;

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child, priority_t priority);

  // Returns true if child was removed successfully.
  bool RemoveChild(TrafficClass *child) override;

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;
  void BlockTowardsRoot() override;

  void FinishAndAccountTowardsRoot(SchedWakeupQueue *wakeup_queue,
                                   TrafficClass *child, resource_arr_t usage,
                                   uint64_t tsc) override;

  const std::vector<ChildData> &children() const { return children_; }

 private:
  size_t
      first_runnable_;  // Index of first member of children_ that is runnable.
  std::vector<ChildData> children_;
};

class WeightedFairTrafficClass final : public TrafficClass {
 public:
  struct ChildData {
    bool operator<(const ChildData &right) const {
      // Reversed so that priority_queue is a min priority queue.
      return right.pass_ < pass_;
    }

    int64_t stride_;
    int64_t pass_;

    TrafficClass *c_;
  };

  WeightedFairTrafficClass(const std::string &name, resource_t resource)
      : TrafficClass(name, POLICY_WEIGHTED_FAIR),
        resource_(resource),
        runnable_children_(),
        blocked_children_(),
        all_children_() {}

  ~WeightedFairTrafficClass();

  std::vector<TrafficClass *> Children() const override;

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child, resource_share_t share);

  // Returns true if child was removed successfully.
  bool RemoveChild(TrafficClass *child) override;

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;
  void BlockTowardsRoot() override;

  void FinishAndAccountTowardsRoot(SchedWakeupQueue *wakeup_queue,
                                   TrafficClass *child, resource_arr_t usage,
                                   uint64_t tsc) override;

  resource_t resource() const { return resource_; }

  void set_resource(resource_t res) { resource_ = res; }

  const extended_priority_queue<ChildData> &runnable_children() const {
    return runnable_children_;
  }

  const std::list<ChildData> &blocked_children() const {
    return blocked_children_;
  }

  const std::vector<std::pair<TrafficClass *, resource_share_t>> &children()
      const {
    return all_children_;
  }

 private:
  // The resource that we are sharing.
  resource_t resource_;

  extended_priority_queue<ChildData> runnable_children_;
  std::list<ChildData> blocked_children_;

  // This is a copy of the pointers to (and shares of) all children. It can be
  // safely accessed from the master thread while the workers are running.
  std::vector<std::pair<TrafficClass *, resource_share_t>> all_children_;
};

class RoundRobinTrafficClass final : public TrafficClass {
 public:
  explicit RoundRobinTrafficClass(const std::string &name)
      : TrafficClass(name, POLICY_ROUND_ROBIN),
        next_child_(),
        runnable_children_(),
        blocked_children_(),
        all_children_() {}

  ~RoundRobinTrafficClass();

  std::vector<TrafficClass *> Children() const override {
    return all_children_;
  }

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child);

  // Returns true if child was removed successfully.
  bool RemoveChild(TrafficClass *child) override;

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;
  void BlockTowardsRoot() override;

  void FinishAndAccountTowardsRoot(SchedWakeupQueue *wakeup_queue,
                                   TrafficClass *child, resource_arr_t usage,
                                   uint64_t tsc) override;

  const std::vector<TrafficClass *> &runnable_children() const {
    return runnable_children_;
  }

  const std::list<TrafficClass *> &blocked_children() const {
    return blocked_children_;
  }

 private:
  size_t next_child_;

  std::vector<TrafficClass *> runnable_children_;
  std::list<TrafficClass *> blocked_children_;

  // This is a copy of the pointers to all children. It can be safely
  // accessed from the master thread while the workers are running.
  std::vector<TrafficClass *> all_children_;
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
        limit_arg_(),
        max_burst_(),
        max_burst_arg_(),
        tokens_(),
        last_tsc_(),
        child_() {
    set_limit(limit);
    set_max_burst(max_burst);
  }

  ~RateLimitTrafficClass();

  std::vector<TrafficClass *> Children() const override;

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child);

  // Returns true if child was removed successfully.
  bool RemoveChild(TrafficClass *child) override;

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;
  void BlockTowardsRoot() override;

  void FinishAndAccountTowardsRoot(SchedWakeupQueue *wakeup_queue,
                                   TrafficClass *child, resource_arr_t usage,
                                   uint64_t tsc) override;

  resource_t resource() const { return resource_; }

  // Return the configured limit, in work units
  uint64_t limit() const { return limit_; }

  // Return the configured max burst, in work units
  uint64_t max_burst() const { return max_burst_; }

  // Return the configured limit, in resource units
  uint64_t limit_arg() const { return limit_arg_; }

  // Return the configured max burst, in resource units
  uint64_t max_burst_arg() const { return max_burst_arg_; }

  void set_resource(resource_t res) { resource_ = res; }

  // Set the limit to `limit`, which is in units of the resource type
  void set_limit(uint64_t limit) {
    limit_arg_ = limit;
    limit_ = to_work_units_per_cycle(limit);
  }

  // Set the max burst to `burst`, which is in units of the resource type
  void set_max_burst(uint64_t burst) {
    max_burst_arg_ = burst;
    max_burst_ = to_work_units(burst);
  }

  TrafficClass *child() const { return child_; }

  // Convert resource units to work units per cycle.
  // Not meant to be used in the datapath: slow due to 128bit operations
  static uint64_t to_work_units_per_cycle(uint64_t x) {
#if INTPTR_MAX == INT64_MAX
    return (static_cast<unsigned __int128>(x) << kUsageAmplifierPow) / tsc_hz;
#elif INTPTR_MAX == INT32_MAX
    // On 32bit systems, __int128 is not available.
    // Instead, we sacrfice the accuracy of tsc_hz to avoid overflow
    return (x << (kUsageAmplifierPow - 10)) / (tsc_hz >> 10);
#else
#error Forgot to add #include <cstdint>?
#endif
  }

  // Convert resource units to work units
  static uint64_t to_work_units(uint64_t x) { return x << kUsageAmplifierPow; }

 private:
  template <typename CallableTask>
  friend class Scheduler;

  static const int kUsageAmplifierPow = 32;

  // The resource that we are limiting.
  resource_t resource_;

  // 1 work unit = 2 ^ kUsageAmplifierPow resource usage.
  // (for better precision without using floating point numbers)
  uint64_t limit_;          // In work units per cycle (0 if unlimited).
  uint64_t limit_arg_;      // In resource units per second.
  uint64_t max_burst_;      // In work units.
  uint64_t max_burst_arg_;  // In resource units.
  uint64_t tokens_;         // In work units.

  // Last time this TC was scheduled.
  uint64_t last_tsc_;

  TrafficClass *child_;
};

template <typename CallableTask>
class LeafTrafficClass final : public TrafficClass {
 public:
  static const uint64_t kInitialWaitCycles = (1ull << 14);

  explicit LeafTrafficClass(const std::string &name, const CallableTask &task)
      : TrafficClass(name, POLICY_LEAF, false),
        task_(task),
        wait_cycles_(kInitialWaitCycles) {
    task_.Attach(this);
  }

  ~LeafTrafficClass() override;

  std::vector<TrafficClass *> Children() const override { return {}; }

  // Returns true if child was removed successfully.
  bool RemoveChild(TrafficClass *) override { return false; }

  TrafficClass *PickNextChild() override { return nullptr; }

  uint64_t wait_cycles() const { return wait_cycles_; }

  void set_wait_cycles(uint64_t wait_cycles) { wait_cycles_ = wait_cycles; }

  void BlockTowardsRoot() override {
    TrafficClass::BlockTowardsRootSetBlocked(false);
  }

  void UnblockTowardsRoot(uint64_t tsc) override {
    TrafficClass::UnblockTowardsRootSetBlocked(tsc, false);
  }

  const CallableTask &task() const { return task_; }

  void FinishAndAccountTowardsRoot(SchedWakeupQueue *wakeup_queue,
                                   [[maybe_unused]] TrafficClass *child,
                                   resource_arr_t usage,
                                   uint64_t tsc) override {
    ACCUMULATE(stats_.usage, usage);
    if (!parent_) {
      return;
    }
    parent_->FinishAndAccountTowardsRoot(wakeup_queue, this, usage, tsc);
  }

 private:
  CallableTask task_;

  uint64_t wait_cycles_;
};

class PriorityChildArgs : public TCChildArgs {
 public:
  PriorityChildArgs(priority_t priority, TrafficClass *c)
      : TCChildArgs(POLICY_PRIORITY, c), priority_(priority) {}
  priority_t priority() { return priority_; }

 private:
  priority_t priority_;
};

class WeightedFairChildArgs : public TCChildArgs {
 public:
  WeightedFairChildArgs(resource_share_t share, TrafficClass *c)
      : TCChildArgs(POLICY_WEIGHTED_FAIR, c), share_(share) {}
  resource_share_t share() { return share_; }

 private:
  resource_share_t share_;
};

class RoundRobinChildArgs : public TCChildArgs {
 public:
  RoundRobinChildArgs(TrafficClass *c) : TCChildArgs(POLICY_ROUND_ROBIN, c) {}
};

class RateLimitChildArgs : public TCChildArgs {
 public:
  RateLimitChildArgs(TrafficClass *c) : TCChildArgs(POLICY_RATE_LIMIT, c) {}
};

// Responsible for creating and destroying all traffic classes.
class TrafficClassBuilder {
 public:
  template <typename T, typename... TArgs>
  static T *CreateTrafficClass(const std::string &name, TArgs... args) {
    if (all_tcs_.count(name)) {
      return nullptr;
    }

    T *c = new T(name, args...);
    all_tcs_.emplace(name, c);
    return c;
  }

  struct PriorityArgs {
    PriorityFakeType dummy;
  };
  struct WeightedFairArgs {
    WeightedFairFakeType dummy;
    resource_t resource;
  };
  struct RoundRobinArgs {
    RoundRobinFakeType dummy;
  };
  struct RateLimitArgs {
    RateLimitFakeType dummy;
    resource_t resource;
    uint64_t limit;
    uint64_t max_burst;
  };
  template <typename CallableTask>
  struct LeafArgs {
    LeafFakeType dummy;
    CallableTask task;
  };

  // These CreateTree(...) functions enable brace-initialized construction of a
  // traffic class hierarchy.  For example,
  //
  //   CreateTree("foo", {PRIORITY}, {{PRIORITY, 10, ...}, {PRIORITY, 15, ...}})
  //
  // creates a tree with a priority root and two children, one of priority 10
  // and one of priority 15, where the ... can contain a similar call to
  // CreateTree to construct any similar subtree.  Classes that require
  // arguments can be constructed as well; for example,
  //
  //   CreateTree("bar", {WEIGHTED_FAIR, RESOURCE_CYCLE}, {{WEIGHTED_FAIR, 3,
  //   ...}})
  //
  // creates a tree with a weighted fair root sharing cycles and one child with
  // a share of 3, with ... being additional calls to CreateTree.
  //
  // No checking is done on the tree to ensure any sort of validity.
  //
  // All classes constructed via these routines are created through calls to
  // CreateTrafficClass above.
  static TrafficClass *CreateTree(const std::string &name,
                                  [[maybe_unused]] PriorityArgs args,
                                  std::vector<PriorityChildArgs> children) {
    PriorityTrafficClass *p = CreateTrafficClass<PriorityTrafficClass>(name);
    for (auto &c : children) {
      p->AddChild(c.child(), c.priority());
    }
    return p;
  }

  static TrafficClass *CreateTree(const std::string &name,
                                  WeightedFairArgs args,
                                  std::vector<WeightedFairChildArgs> children) {
    WeightedFairTrafficClass *p =
        CreateTrafficClass<WeightedFairTrafficClass>(name, args.resource);
    for (auto &c : children) {
      p->AddChild(c.child(), c.share());
    }
    return p;
  }

  static TrafficClass *CreateTree(const std::string &name,
                                  [[maybe_unused]] RoundRobinArgs args,
                                  std::vector<RoundRobinChildArgs> children =
                                      std::vector<RoundRobinChildArgs>()) {
    RoundRobinTrafficClass *p =
        CreateTrafficClass<RoundRobinTrafficClass>(name);
    for (auto &c : children) {
      p->AddChild(c.child());
    }
    return p;
  }

  static TrafficClass *CreateTree(const std::string &name, RateLimitArgs args,
                                  RateLimitChildArgs child) {
    RateLimitTrafficClass *p = CreateTrafficClass<RateLimitTrafficClass>(
        name, args.resource, args.limit, args.max_burst);
    p->AddChild(child.child());
    return p;
  }

  template <typename CallableTask>
  static TrafficClass *CreateTree(const std::string &name,
                                  LeafArgs<CallableTask> args) {
    return CreateTrafficClass<LeafTrafficClass<CallableTask>>(name, args.task);
  }

  // Attempts to clear knowledge of all classes.  Returns true upon success.
  // Frees all TrafficClass objects that were created by this builder.
  static bool ClearAll();

  // Attempts to clear knowledge of given class.  Returns true upon success.
  static bool Clear(TrafficClass *c);

  static const std::unordered_map<std::string, TrafficClass *> &all_tcs() {
    return all_tcs_;
  }

  // Returns the TrafficClass * with the given name or nullptr if not found.
  static TrafficClass *Find(const std::string &name) {
    auto it = all_tcs_.find(name);
    if (it != all_tcs_.end()) {
      return it->second;
    }
    return nullptr;
  }

 private:
  // A collection of all TCs in the system, mapped from their textual name.
  static std::unordered_map<std::string, TrafficClass *> all_tcs_;
};

template <typename CallableTask>
LeafTrafficClass<CallableTask>::~LeafTrafficClass() {
  TrafficClassBuilder::Clear(this);
  task_.Detach();
}

}  // namespace bess

#endif  // BESS_TRAFFIC_CLASS_H_
