// Unit tests traffic class and scheduler routines.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "scheduler.h"
#include "traffic_class.h"

#define CT TrafficClassBuilder::CreateTree

using namespace bess::traffic_class_initializer_types;

namespace bess {

// Tests that we can create a leaf node.
TEST(CreateTree, Leaf) {
  std::unique_ptr<TrafficClass> c(CT("leaf", {LEAF}));
  ASSERT_NE(nullptr, c.get());
  ASSERT_EQ(1, c->Size());
  EXPECT_EQ(POLICY_LEAF, c->policy());

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a priority root node with a leaf under it.
TEST(CreateTree, PriorityRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(
      CT("root", {PRIORITY}, {{PRIORITY, 10, CT("leaf", {LEAF})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, tree);
  EXPECT_EQ(POLICY_PRIORITY, tree->policy());

  PriorityTrafficClass *c = static_cast<PriorityTrafficClass *>(tree.get());
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(1, c->children().size());
  EXPECT_EQ(10, c->children()[0].priority_);

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->children()[0].c_);
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);
  ASSERT_EQ(1, TrafficClassBuilder::Find("leaf")->Size());

  // We shouldn't be able to add a child with a duplicate priority.
  TrafficClass *leaf2 = CT("leaf_2", {LEAF});
  ASSERT_FALSE(c->AddChild(leaf2, 10));
  delete leaf2;

  // A different priority should be fine.
  TrafficClass *leaf3 = CT("leaf_3", {LEAF});
  ASSERT_TRUE(c->AddChild(leaf3, 2));
  ASSERT_EQ(1, TrafficClassBuilder::Find("leaf_3")->Size());

  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a weighted fair root node with a leaf
// under it.
TEST(CreateTree, WeightedFairRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(
      CT("root", {WEIGHTED_FAIR, RESOURCE_CYCLE},
         {{WEIGHTED_FAIR, 10, CT("leaf", {LEAF})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, tree);
  EXPECT_EQ(POLICY_WEIGHTED_FAIR, tree->policy());

  WeightedFairTrafficClass *c =
      static_cast<WeightedFairTrafficClass *>(tree.get());
  ASSERT_NE(nullptr, c);
  EXPECT_EQ(RESOURCE_CYCLE, c->resource());
  ASSERT_EQ(0, c->children().size());
  ASSERT_EQ(1, c->blocked_children().size());
  EXPECT_EQ(STRIDE1 / 10, c->blocked_children().front().stride_);

  LeafTrafficClass *leaf =
      static_cast<LeafTrafficClass *>(c->blocked_children().front().c_);
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a round robin root node with a leaf under
// it.
TEST(CreateTree, RoundRobinRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(
      CT("root", {ROUND_ROBIN}, {{ROUND_ROBIN, CT("leaf", {LEAF})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, tree);
  EXPECT_EQ(POLICY_ROUND_ROBIN, tree->policy());

  RoundRobinTrafficClass *c = static_cast<RoundRobinTrafficClass *>(tree.get());
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(0, c->children().size());
  ASSERT_EQ(1, c->blocked_children().size());

  LeafTrafficClass *leaf =
      static_cast<LeafTrafficClass *>(c->blocked_children().front());
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a rate limit root node with a leaf under
// it.
TEST(CreateTree, RateLimitRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(CT("root",
                                        {RATE_LIMIT, RESOURCE_CYCLE, 10, 15},
                                        {RATE_LIMIT, CT("leaf", {LEAF})}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, tree);
  EXPECT_EQ(POLICY_RATE_LIMIT, tree->policy());

  RateLimitTrafficClass *c = static_cast<RateLimitTrafficClass *>(tree.get());
  ASSERT_NE(nullptr, c);
  EXPECT_EQ(RESOURCE_CYCLE, c->resource());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->child());
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(SchedulerNext, BasicTreePriority) {
  Scheduler s(CT("root", {PRIORITY}, {{PRIORITY, 10, CT("leaf", {LEAF})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, s.root());
  EXPECT_EQ(POLICY_PRIORITY, s.root()->policy());

  PriorityTrafficClass *c = static_cast<PriorityTrafficClass *>(s.root());
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(1, c->children().size());
  EXPECT_EQ(10, c->children()[0].priority_);

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->children()[0].c_);
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  // Leaf should be blocked until there is a task.
  ASSERT_TRUE(leaf->blocked());
  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  // Adding a fake task should unblock the whole tree.
  leaf->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next(rdtsc()));

  EXPECT_TRUE(leaf->RemoveTask(reinterpret_cast<Task *>(1)));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(SchedulerNext, BasicTreeWeightedFair) {
  Scheduler s(CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
                 {{WEIGHTED_FAIR, 2, CT("leaf", {LEAF})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, s.root());
  EXPECT_EQ(POLICY_WEIGHTED_FAIR, s.root()->policy());

  WeightedFairTrafficClass *c =
      static_cast<WeightedFairTrafficClass *>(s.root());
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(0, c->children().size());
  ASSERT_EQ(1, c->blocked_children().size());

  LeafTrafficClass *leaf =
      static_cast<LeafTrafficClass *>(c->blocked_children().front().c_);
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  // Leaf should be blocked until there is a task.
  ASSERT_TRUE(leaf->blocked());
  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  // Adding a fake task should unblock the whole tree.
  leaf->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next(rdtsc()));

  EXPECT_TRUE(leaf->RemoveTask(reinterpret_cast<Task *>(1)));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(SchedulerNext, BasicTreeRoundRobin) {
  Scheduler s(CT("root", {ROUND_ROBIN}, {{ROUND_ROBIN, CT("leaf", {LEAF})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, s.root());
  EXPECT_EQ(POLICY_ROUND_ROBIN, s.root()->policy());

  RoundRobinTrafficClass *c = static_cast<RoundRobinTrafficClass *>(s.root());
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(1, c->blocked_children().size());

  LeafTrafficClass *leaf =
      static_cast<LeafTrafficClass *>(c->blocked_children().front());
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  // Leaf should be blocked until there is a task.
  ASSERT_TRUE(leaf->blocked());
  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  // Adding a fake task should unblock the whole tree.
  leaf->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next(rdtsc()));

  EXPECT_TRUE(leaf->RemoveTask(reinterpret_cast<Task *>(1)));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(SchedulerNext, BasicTreeRateLimit) {
  uint64_t new_limit = 25;
  uint64_t new_burst = 50;
  Scheduler s(CT("root", {RATE_LIMIT, RESOURCE_COUNT, 50, 100},
                 {RATE_LIMIT, CT("leaf", {LEAF})}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, s.root());
  EXPECT_EQ(POLICY_RATE_LIMIT, s.root()->policy());

  RateLimitTrafficClass *c = static_cast<RateLimitTrafficClass *>(s.root());
  ASSERT_NE(nullptr, c);

  ASSERT_EQ(RESOURCE_COUNT, c->resource());
  ASSERT_EQ(50, c->limit_arg());
  ASSERT_EQ(RateLimitTrafficClass::to_work_units(50), c->limit());
  ASSERT_EQ(100, c->max_burst_arg());
  ASSERT_EQ(RateLimitTrafficClass::to_work_units(100), c->max_burst());

  c->set_resource(RESOURCE_PACKET);
  c->set_limit(new_limit);
  c->set_max_burst(new_burst);

  ASSERT_EQ(RESOURCE_PACKET, c->resource());
  ASSERT_EQ(new_limit, c->limit_arg());
  ASSERT_EQ(RateLimitTrafficClass::to_work_units(new_limit), c->limit());
  ASSERT_EQ(new_burst, c->max_burst_arg());
  ASSERT_EQ(RateLimitTrafficClass::to_work_units(new_burst), c->max_burst());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->child());
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  // Leaf should be blocked until there is a task.
  ASSERT_TRUE(leaf->blocked());
  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  // Adding a fake task should unblock the whole tree.
  leaf->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next(rdtsc()));

  EXPECT_TRUE(leaf->RemoveTask(reinterpret_cast<Task *>(1)));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the
// unblocked leaf repeatedly if one of the leaves is blocked.
TEST(SchedulerNext, TwoLeavesWeightedFairOneBlocked) {
  Scheduler s(CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
                 {{WEIGHTED_FAIR, 1, CT("leaf_1", {LEAF})},
                  {WEIGHTED_FAIR, 2, CT("leaf_2", {LEAF})}}));
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  LeafTrafficClass *leaf_1 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_NE(nullptr, leaf_1);
  ASSERT_TRUE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  ASSERT_NE(nullptr, leaf_2);
  ASSERT_TRUE(leaf_2->blocked());

  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  // Adding a fake task should unblock the whole tree.
  leaf_1->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_1->blocked());

  EXPECT_EQ(leaf_1, s.Next(rdtsc()));

  EXPECT_TRUE(leaf_1->RemoveTask(reinterpret_cast<Task *>(1)));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the
// leaves in proportion to their weights.
TEST(ScheduleOnce, TwoLeavesWeightedFair) {
  Scheduler s(CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
                 {{WEIGHTED_FAIR, 2, CT("leaf_1", {LEAF})},
                  {WEIGHTED_FAIR, 5, CT("leaf_2", {LEAF})}}));
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  LeafTrafficClass *leaf_1 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_NE(nullptr, leaf_1);
  ASSERT_TRUE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  ASSERT_NE(nullptr, leaf_2);
  ASSERT_TRUE(leaf_2->blocked());

  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  // Unblock both leaves.
  leaf_2->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_2->blocked());
  leaf_1->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_1->blocked());

  // Clear out the tasks so that they don't get called during execution.
  leaf_1->tasks().clear();
  leaf_2->tasks().clear();

  WeightedFairTrafficClass *root =
      static_cast<WeightedFairTrafficClass *>(s.root());
  ASSERT_EQ(2, root->children().size());

  // There's no guarantee which will run first because they will tie, so this is
  // a guess based upon the heap's behavior.
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the best
// (lowest) priority leaf that is unblocked at that time.
TEST(ScheduleOnce, TwoLeavesPriority) {
  Scheduler s(CT("root", {PRIORITY}, {{PRIORITY, 0, CT("leaf_1", {LEAF})},
                                      {PRIORITY, 1, CT("leaf_2", {LEAF})}}));
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  LeafTrafficClass *leaf_1 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_NE(nullptr, leaf_1);
  ASSERT_TRUE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  ASSERT_NE(nullptr, leaf_2);
  ASSERT_TRUE(leaf_2->blocked());

  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  // Unblock the second leaf.
  leaf_2->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_2->blocked());
  leaf_2->tasks().clear();

  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));

  // Unblock the first leaf, which should now get picked.
  leaf_1->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_1->blocked());
  leaf_1->tasks().clear();

  ASSERT_EQ(leaf_1, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the
// leaves round robin.
TEST(ScheduleOnce, TwoLeavesRoundRobin) {
  Scheduler s(CT("root", {ROUND_ROBIN}, {{ROUND_ROBIN, CT("leaf_1", {LEAF})},
                                         {ROUND_ROBIN, CT("leaf_2", {LEAF})}}));
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  LeafTrafficClass *leaf_1 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_NE(nullptr, leaf_1);
  ASSERT_TRUE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  ASSERT_NE(nullptr, leaf_2);
  ASSERT_TRUE(leaf_2->blocked());

  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  // Unblock both leaves.
  leaf_1->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_1->blocked());
  leaf_2->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_2->blocked());

  // Clear out the tasks so that they don't get called during execution.
  leaf_1->tasks().clear();
  leaf_2->tasks().clear();

  RoundRobinTrafficClass *root =
      static_cast<RoundRobinTrafficClass *>(s.root());
  ASSERT_EQ(2, root->children().size());

  ASSERT_EQ(leaf_1, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a more complex tree and have the scheduler pick the
// leaves in proportion to their weights even when they are multiple levels down
// in the hierarchy.
TEST(ScheduleOnce, LeavesWeightedFairAndRoundRobin) {
  Scheduler s(
      CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
         {{WEIGHTED_FAIR, 2,
           CT("rr_1", {ROUND_ROBIN}, {{ROUND_ROBIN, CT("leaf_1a", {LEAF})},
                                      {ROUND_ROBIN, CT("leaf_1b", {LEAF})}})},
          {WEIGHTED_FAIR, 5, CT("rr_2", {ROUND_ROBIN},
                                {
                                    {ROUND_ROBIN, CT("leaf_2a", {LEAF})},
                                    {ROUND_ROBIN, CT("leaf_2b", {LEAF})},
                                })}}));
  ASSERT_EQ(7, TrafficClassBuilder::Find("root")->Size());

  std::map<std::string, LeafTrafficClass *> leaves;
  for (auto &leaf_name : {"leaf_1a", "leaf_1b", "leaf_2a", "leaf_2b"}) {
    LeafTrafficClass *leaf =
        static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find(leaf_name));
    ASSERT_NE(nullptr, leaf);
    leaves[leaf_name] = leaf;

    ASSERT_TRUE(leaf->blocked());

    // Unblock leaf by adding a fake task.
    leaf->AddTask(reinterpret_cast<Task *>(1));
    ASSERT_FALSE(leaf->blocked());

    // Clear out the task so that it doesn't get called during execution.
    leaf->tasks().clear();
  }

  WeightedFairTrafficClass *root =
      static_cast<WeightedFairTrafficClass *>(s.root());
  ASSERT_EQ(2, root->children().size());
  RoundRobinTrafficClass *rr_1 =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr_1"));
  ASSERT_EQ(2, rr_1->children().size());
  RoundRobinTrafficClass *rr_2 =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr_2"));
  ASSERT_EQ(2, rr_2->children().size());

  // There's no guarantee which will run first because they will tie, so this is
  // a guess based upon the heap's behavior.
  ASSERT_EQ(leaves["leaf_1a"], s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaves["leaf_2a"], s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaves["leaf_2b"], s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaves["leaf_2a"], s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaves["leaf_1b"], s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaves["leaf_2b"], s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaves["leaf_2a"], s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tests that rate limit nodes get properly blocked and unblocked.
TEST(RateLimit, BasicBlockUnblock) {
  Scheduler s(
      CT("root", {ROUND_ROBIN},
         {{ROUND_ROBIN, CT("limit_1", {RATE_LIMIT, RESOURCE_COUNT, 1, 0},
                           {RATE_LIMIT, CT("leaf_1", {LEAF})})},
          {ROUND_ROBIN, CT("limit_2", {RATE_LIMIT, RESOURCE_COUNT, 1, 0},
                           {RATE_LIMIT, CT("leaf_2", {LEAF})})}}));
  ASSERT_EQ(5, TrafficClassBuilder::Find("root")->Size());
  RoundRobinTrafficClass *rr =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("root"));
  ASSERT_NE(nullptr, rr);

  LeafTrafficClass *leaf_1 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  leaf_1->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_1->blocked());
  leaf_1->tasks().clear();
  ASSERT_FALSE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  leaf_2->AddTask(reinterpret_cast<Task *>(1));
  ASSERT_FALSE(leaf_2->blocked());
  leaf_2->tasks().clear();
  ASSERT_FALSE(leaf_2->blocked());

  RateLimitTrafficClass *limit_1 = static_cast<RateLimitTrafficClass *>(
      TrafficClassBuilder::Find("limit_1"));
  RateLimitTrafficClass *limit_2 = static_cast<RateLimitTrafficClass *>(
      TrafficClassBuilder::Find("limit_2"));

  uint64_t now = rdtsc();
  ASSERT_FALSE(limit_1->blocked());
  ASSERT_FALSE(limit_2->blocked());

  // Schedule once.
  TrafficClass *c = s.Next(now);
  ASSERT_EQ(c, leaf_1);
  resource_arr_t usage;
  usage[RESOURCE_COUNT] = 1;
  c->FinishAndAccountTowardsRoot(&s, nullptr, usage, now);
  ASSERT_TRUE(limit_1->blocked());

  // Fake quarter second delay, schedule again.
  now += tsc_hz / 4;
  c = s.Next(now);
  ASSERT_EQ(c, leaf_2);
  c->FinishAndAccountTowardsRoot(&s, nullptr, usage, now);
  ASSERT_TRUE(limit_2->blocked());

  // The leaves should be unaffected by the rate limiters, but the root
  ASSERT_FALSE(leaf_1->blocked());
  ASSERT_FALSE(leaf_2->blocked());
  ASSERT_TRUE(rr->blocked());

  // Fake a quarter second delay, schedule again.
  now += tsc_hz / 4;
  c = s.Next(now);
  ASSERT_EQ(nullptr, c);

  // Fake a second delay, schedule again and expect unblocking.
  now += tsc_hz * 2;
  c = s.Next(now);
  ASSERT_FALSE(leaf_1->blocked());
  ASSERT_FALSE(leaf_2->blocked());
  ASSERT_FALSE(limit_1->blocked())
      << "tsc=" << now
      << ", limit_1 expiration=" << limit_1->throttle_expiration();
  ASSERT_FALSE(limit_2->blocked())
      << "tsc=" << now
      << ", limit_2 expiration=" << limit_2->throttle_expiration();
  ASSERT_FALSE(rr->blocked());

  EXPECT_EQ(c, leaf_1);
  c->FinishAndAccountTowardsRoot(&s, nullptr, usage, now);
  now += tsc_hz / 4;
  c = s.Next(now);
  ASSERT_EQ(c, leaf_2);

  TrafficClassBuilder::ClearAll();
}

}  // namespace bess
