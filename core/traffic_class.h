#ifndef BESS_TRAFFIC_CLASS_H_
#define BESS_TRAFFIC_CLASS_H_

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

// A large default priority.
#define DEFAULT_PRIORITY 0xFFFFFFFFu

#define USAGE_AMPLIFIER_POW 32

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

class Scheduler;
class TrafficClassBuilder;
class PriorityTrafficClass;
class WeightedFairTrafficClass;
class RoundRobinTrafficClass;
class RateLimitTrafficClass;
class LeafTrafficClass;
class TrafficClass;

typedef void (*TraverseTcFn)(const TrafficClass *, void *);

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

// A TrafficClass represents a hierarchy of TrafficClasses which contain
// schedulable task units.
class TrafficClass {
 public:
  virtual ~TrafficClass() {}

  virtual void Traverse(TraverseTcFn f, void *arg) const { f(this, arg); }

  // Returns the number of TCs in the TC subtree rooted at this, including
  // this TC.
  size_t Size() const;

  // Returns the root of the tree this class belongs to.
  // Expensive in that it is recursive, so do not call from
  // performance-sensitive code.
  TrafficClass *Root() {
    if (!parent_) {
      return this;
    }
    return parent_->Root();
  }

  // Starts from the current node and accounts for the usage of the given child
  // after execution and finishes any data structure reorganization required
  // after execution has finished.  The scheduler is the scheduler that owns
  // these classes.
  virtual void FinishAndAccountTowardsRoot(Scheduler *sched,
                                           TrafficClass *child,
                                           resource_arr_t usage,
                                           uint64_t tsc) = 0;

  inline TrafficClass *parent() const { return parent_; }

  inline const std::string &name() const { return name_; }

  inline const struct tc_stats &stats() const { return stats_; }

  inline bool blocked() const { return blocked_; }

  inline TrafficPolicy policy() const { return policy_; }

 protected:
  friend PriorityTrafficClass;
  friend WeightedFairTrafficClass;
  friend RoundRobinTrafficClass;
  friend RateLimitTrafficClass;
  friend LeafTrafficClass;

  TrafficClass(const std::string &name, const TrafficPolicy &policy)
      : parent_(), name_(name), stats_(), blocked_(true), policy_(policy) {}

  // Sets blocked status to nowblocked and recurses towards root if our blocked
  // status changed.
  void UnblockTowardsRootSetBlocked(uint64_t tsc, bool nowblocked)
      __attribute__((always_inline)) {
    bool became_unblocked = !nowblocked && blocked_;
    blocked_ = nowblocked;

    if (!parent_ || !became_unblocked) {
      return;
    }

    parent_->UnblockTowardsRoot(tsc);
  }

  // Increments the TC count from this up towards the root.
  void IncrementTcCountTowardsRoot(int increment);

  // Returns the next schedulable child of this traffic class.
  virtual TrafficClass *PickNextChild() = 0;

  // Starts from the current node and attempts to recursively unblock (if
  // eligible) all nodes from this node to the root.
  virtual void UnblockTowardsRoot(uint64_t tsc) = 0;

  // Parent of this class; nullptr for root.
  TrafficClass *parent_;

  // The name given to this class.
  std::string name_;

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
  struct ChildData {
    bool operator<(const ChildData &right) const
        __attribute__((always_inline)) {
      return priority_ < right.priority_;
    }

    priority_t priority_;
    TrafficClass *c_;
  };

  explicit PriorityTrafficClass(const std::string &name)
      : TrafficClass(name, POLICY_PRIORITY), first_runnable_(0), children_() {}

  ~PriorityTrafficClass();

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child, priority_t priority);

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;

  void FinishAndAccountTowardsRoot(Scheduler *sched, TrafficClass *child,
                                   resource_arr_t usage, uint64_t tsc) override;

  const std::vector<ChildData> &children() const { return children_; }

  void Traverse(TraverseTcFn f, void *arg) const override;

 private:
  friend Scheduler;

  size_t
      first_runnable_;  // Index of first member of children_ that is runnable.
  std::vector<ChildData> children_;
};

class WeightedFairTrafficClass final : public TrafficClass {
 public:
  struct ChildData {
    bool operator<(const ChildData &right) const
        __attribute__((always_inline)) {
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
        children_(),
        blocked_children_() {}

  ~WeightedFairTrafficClass();

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child, resource_share_t share);

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;

  void FinishAndAccountTowardsRoot(Scheduler *sched, TrafficClass *child,
                                   resource_arr_t usage, uint64_t tsc) override;

  resource_t resource() const { return resource_; }

  const extended_priority_queue<ChildData> &children() const {
    return children_;
  }

  const std::list<ChildData> &blocked_children() const {
    return blocked_children_;
  }

  void Traverse(TraverseTcFn f, void *arg) const override;

 private:
  friend Scheduler;

  // The resource that we are sharing.
  resource_t resource_;

  extended_priority_queue<ChildData> children_;
  std::list<ChildData> blocked_children_;
};

class RoundRobinTrafficClass final : public TrafficClass {
 public:
  explicit RoundRobinTrafficClass(const std::string &name)
      : TrafficClass(name, POLICY_ROUND_ROBIN),
        next_child_(),
        children_(),
        blocked_children_() {}

  ~RoundRobinTrafficClass();

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child);

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;

  void FinishAndAccountTowardsRoot(Scheduler *sched, TrafficClass *child,
                                   resource_arr_t usage, uint64_t tsc) override;

  const std::vector<TrafficClass *> &children() const { return children_; }

  const std::list<TrafficClass *> &blocked_children() const {
    return blocked_children_;
  }

  void Traverse(TraverseTcFn f, void *arg) const override;

 private:
  friend Scheduler;

  size_t next_child_;
  std::vector<TrafficClass *> children_;
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
        limit_arg_(),
        max_burst_(),
        max_burst_arg_(),
        tokens_(),
        throttle_expiration_(),
        last_tsc_(),
        child_() {
    limit_arg_ = limit;
    limit_ = to_work_units(limit);
    if (limit_) {
      max_burst_arg_ = max_burst;
      max_burst_ = to_work_units(max_burst);
    }
  }

  ~RateLimitTrafficClass();

  // Returns true if child was added successfully.
  bool AddChild(TrafficClass *child);

  inline uint64_t throttle_expiration() const { return throttle_expiration_; }

  TrafficClass *PickNextChild() override;

  void UnblockTowardsRoot(uint64_t tsc) override;

  void FinishAndAccountTowardsRoot(Scheduler *sched, TrafficClass *child,
                                   resource_arr_t usage, uint64_t tsc) override;

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
    limit_ = to_work_units(limit);
  }

  // Set the max burst to `burst`, which is in units of the resource type
  void set_max_burst(uint64_t burst) {
    max_burst_arg_ = burst;
    max_burst_ = to_work_units(burst);
  }

  TrafficClass *child() const { return child_; }

  void Traverse(TraverseTcFn f, void *arg) const override;

  // Convert resource units to work units
  static uint64_t to_work_units(uint64_t x) {
    return (x << (USAGE_AMPLIFIER_POW - 4)) / (tsc_hz >> 4);
  }

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
  uint64_t limit_;          // In work units per cycle (0 if unlimited).
  uint64_t limit_arg_;      // In resource units per second.
  uint64_t max_burst_;      // In work units per cycle (0 if unlimited).
  uint64_t max_burst_arg_;  // In resource units per second.
  uint64_t tokens_;         // In work units.

  uint64_t throttle_expiration_;

  // Last time this TC was scheduled.
  uint64_t last_tsc_;

  TrafficClass *child_;
};

class LeafTrafficClass final : public TrafficClass {
 public:
  explicit LeafTrafficClass(const std::string &name)
      : TrafficClass(name, POLICY_LEAF), task_index_(), tasks_() {}

  ~LeafTrafficClass();

  // Direct access to the tasks vector, for testing only.
  std::vector<Task *> &tasks() { return tasks_; }

  // Regular accessor for everyone else
  const std::vector<Task *> &tasks() const { return tasks_; }

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
    TrafficClass::UnblockTowardsRootSetBlocked(tsc, tasks_.empty());
    return;
  }

  void FinishAndAccountTowardsRoot(Scheduler *sched,
                                   [[maybe_unused]] TrafficClass *child,
                                   resource_arr_t usage,
                                   uint64_t tsc) override {
    ACCUMULATE(stats_.usage, usage);
    parent_->FinishAndAccountTowardsRoot(sched, this, usage, tsc);
  }

 private:
  friend Scheduler;
  friend TrafficClassBuilder;

  // The tasks of this class.  Always empty if this is a non-leaf class.
  // task_index_ keeps track of the next task to run.
  size_t task_index_;
  std::vector<Task *> tasks_;
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
    tc_count_[c] = 1;
    return c;
  }

  struct PriorityArgs {
    PriorityFakeType dummy;
  };
  struct PriorityChildArgs {
    PriorityFakeType dummy;
    priority_t priority;
    TrafficClass *c;
  };

  struct WeightedFairArgs {
    WeightedFairFakeType dummy;
    resource_t resource;
  };
  struct WeightedFairChildArgs {
    WeightedFairFakeType dummy;
    resource_share_t share;
    TrafficClass *c;
  };

  struct RoundRobinArgs {
    RoundRobinFakeType dummy;
  };
  struct RoundRobinChildArgs {
    RoundRobinFakeType dummy;
    TrafficClass *c;
  };

  struct RateLimitArgs {
    RateLimitFakeType dummy;
    resource_t resource;
    uint64_t limit;
    uint64_t max_burst;
  };
  struct RateLimitChildArgs {
    RateLimitFakeType dummy;
    TrafficClass *c;
  };

  struct LeafArgs {
    LeafFakeType dummy;
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
      p->AddChild(c.c, c.priority);
    }
    return p;
  }

  static TrafficClass *CreateTree(const std::string &name,
                                  WeightedFairArgs args,
                                  std::vector<WeightedFairChildArgs> children) {
    WeightedFairTrafficClass *p =
        CreateTrafficClass<WeightedFairTrafficClass>(name, args.resource);
    for (auto &c : children) {
      p->AddChild(c.c, c.share);
    }
    return p;
  }

  static TrafficClass *CreateTree(const std::string &name,
                                  [[maybe_unused]] RoundRobinArgs args,
                                  std::vector<RoundRobinChildArgs> children) {
    RoundRobinTrafficClass *p =
        CreateTrafficClass<RoundRobinTrafficClass>(name);
    for (auto &c : children) {
      p->AddChild(c.c);
    }
    return p;
  }

  static TrafficClass *CreateTree(const std::string &name, RateLimitArgs args,
                                  RateLimitChildArgs child) {
    RateLimitTrafficClass *p = CreateTrafficClass<RateLimitTrafficClass>(
        name, args.resource, args.limit, args.max_burst);
    p->AddChild(child.c);
    return p;
  }

  static TrafficClass *CreateTree(const std::string &name,
                                  [[maybe_unused]] LeafArgs args) {
    return CreateTrafficClass<LeafTrafficClass>(name);
  }

  // Attempts to clear knowledge of all classes.  Returns true upon success.
  // Frees all TrafficClass objects that were created by this builder.
  static bool ClearAll();

  // Attempts to clear knowledge of given class.  Returns true upon success.
  static bool Clear(TrafficClass *c);

  static const std::unordered_map<std::string, TrafficClass *>
      &all_tcs() {
    return all_tcs_;
  }

  static std::unordered_map<const TrafficClass *, int> &tc_count() {
    return tc_count_;
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

  // A mapping from each TC to the count of TCs in its subtree, including itself.
  static std::unordered_map<const TrafficClass *, int> tc_count_;
};

}  // namespace bess

#endif  // BESS_TRAFFIC_CLASS_H_
