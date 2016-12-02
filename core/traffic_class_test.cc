// Unit tests traffic class and scheduler routines.

#include "scheduler.h"
#include "traffic_class.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#define CT TrafficClassBuilder::CreateTree

using namespace bess::traffic_class_initializer_types;

namespace bess {

// Tests that we can create and fetch a priority root node with a leaf under it.
TEST(CreateTree, PriorityRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(CT("root", {PRIORITY}, {{PRIORITY, 10, CT("leaf", {LEAF})}}));

  ASSERT_TRUE(tree != nullptr);
  EXPECT_EQ(POLICY_PRIORITY, tree->policy());

  PriorityTrafficClass *c = static_cast<PriorityTrafficClass *>(tree.get());
  ASSERT_TRUE(c != nullptr);
  ASSERT_EQ(1, c->children().size());
  EXPECT_EQ(10, c->children()[0].priority_);

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->children()[0].c_);
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a weighted fair root node with a leaf under it.
TEST(CreateTree, WeightedFairRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(CT("root", {WEIGHTED_FAIR, RESOURCE_CYCLE}, {{WEIGHTED_FAIR, 10, CT("leaf", {LEAF})}}));

  ASSERT_TRUE(tree != nullptr);
  EXPECT_EQ(POLICY_WEIGHTED_FAIR, tree->policy());

  WeightedFairTrafficClass *c = static_cast<WeightedFairTrafficClass *>(tree.get());
  ASSERT_TRUE(c != nullptr);
  EXPECT_EQ(RESOURCE_CYCLE, c->resource());
  ASSERT_EQ(0, c->children().size());
  ASSERT_EQ(1, c->blocked_children().size());
  EXPECT_EQ(STRIDE1 / 10, c->blocked_children().front().stride_);

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->blocked_children().front().c_);
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a round robin root node with a leaf under it.
TEST(CreateTree, RoundRobinRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(CT("root", {ROUND_ROBIN}, {{ROUND_ROBIN, CT("leaf", {LEAF})}}));

  ASSERT_TRUE(tree != nullptr);
  EXPECT_EQ(POLICY_ROUND_ROBIN, tree->policy());

  RoundRobinTrafficClass *c = static_cast<RoundRobinTrafficClass *>(tree.get());
  ASSERT_TRUE(c != nullptr);
  ASSERT_EQ(0, c->children().size());
  ASSERT_EQ(1, c->blocked_children().size());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->blocked_children().front());
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a rate limit root node with a leaf under it.
TEST(CreateTree, RateLimitRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(CT("root", {RATE_LIMIT, RESOURCE_CYCLE, 10, 15}, {RATE_LIMIT, CT("leaf", {LEAF})}));

  ASSERT_TRUE(tree != nullptr);
  EXPECT_EQ(POLICY_RATE_LIMIT, tree->policy());

  RateLimitTrafficClass *c = static_cast<RateLimitTrafficClass *>(tree.get());
  ASSERT_TRUE(c != nullptr);
  EXPECT_EQ(RESOURCE_CYCLE, c->resource());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->child());
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(SchedulerNext, BasicTreePriority) {
  Scheduler s(CT("root", {PRIORITY}, {{PRIORITY, 10, CT("leaf", {LEAF})}}));

  ASSERT_TRUE(s.root() != nullptr);
  EXPECT_EQ(POLICY_PRIORITY, s.root()->policy());

  PriorityTrafficClass *c = static_cast<PriorityTrafficClass *>(s.root());
  ASSERT_TRUE(c != nullptr);
  ASSERT_EQ(1, c->children().size());
  EXPECT_EQ(10, c->children()[0].priority_);

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->children()[0].c_);
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), c);

  // Leaf should be blocked until there is a task.
  ASSERT_TRUE(leaf->blocked());
  EXPECT_EQ(nullptr, s.Next());

  // Adding a fake task should unblock the whole tree.
  leaf->AddTask((Task *) 1);
  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next());

  EXPECT_TRUE(leaf->RemoveTask((Task *) 1));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(SchedulerNext, BasicTreeWeightedFair) {
  Scheduler s(CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT}, {{WEIGHTED_FAIR, 2, CT("leaf", {LEAF})}}));

  ASSERT_TRUE(s.root() != nullptr);
  EXPECT_EQ(POLICY_WEIGHTED_FAIR, s.root()->policy());

  WeightedFairTrafficClass *c = static_cast<WeightedFairTrafficClass *>(s.root());
  ASSERT_TRUE(c != nullptr);
  ASSERT_EQ(0, c->children().size());
  ASSERT_EQ(1, c->blocked_children().size());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->blocked_children().front().c_);
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), c);

  // Leaf should be blocked until there is a task.
  ASSERT_TRUE(leaf->blocked());
  EXPECT_EQ(nullptr, s.Next());

  // Adding a fake task should unblock the whole tree.
  leaf->AddTask((Task *) 1);
  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next());

  EXPECT_TRUE(leaf->RemoveTask((Task *) 1));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(SchedulerNext, BasicTreeRoundRobin) {
  Scheduler s(CT("root", {ROUND_ROBIN}, {{ROUND_ROBIN, CT("leaf", {LEAF})}}));

  ASSERT_TRUE(s.root() != nullptr);
  EXPECT_EQ(POLICY_ROUND_ROBIN, s.root()->policy());

  RoundRobinTrafficClass *c = static_cast<RoundRobinTrafficClass *>(s.root());
  ASSERT_TRUE(c != nullptr);
  ASSERT_EQ(1, c->blocked_children().size());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->blocked_children().front());
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), c);

  // Leaf should be blocked until there is a task.
  ASSERT_TRUE(leaf->blocked());
  EXPECT_EQ(nullptr, s.Next());

  // Adding a fake task should unblock the whole tree.
  leaf->AddTask((Task *) 1);
  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next());

  EXPECT_TRUE(leaf->RemoveTask((Task *) 1));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(SchedulerNext, BasicTreeRateLimit) {
  Scheduler s(CT("root", {RATE_LIMIT, RESOURCE_COUNT, 50, 100}, {RATE_LIMIT, CT("leaf", {LEAF})}));

  ASSERT_TRUE(s.root() != nullptr);
  EXPECT_EQ(POLICY_RATE_LIMIT, s.root()->policy());

  RateLimitTrafficClass *c = static_cast<RateLimitTrafficClass *>(s.root());
  ASSERT_TRUE(c != nullptr);

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->child());
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), c);

  // Leaf should be blocked until there is a task.
  ASSERT_TRUE(leaf->blocked());
  EXPECT_EQ(nullptr, s.Next());

  // Adding a fake task should unblock the whole tree.
  leaf->AddTask((Task *) 1);
  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next());

  EXPECT_TRUE(leaf->RemoveTask((Task *) 1));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the
// unblocked leaf repeatedly if one of the leaves is blocked.
TEST(SchedulerNext, TwoLeavesWeightedFairOneBlocked) {
  Scheduler s(CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
              {{WEIGHTED_FAIR, 1, CT("leaf_1", {LEAF})},
               {WEIGHTED_FAIR, 2, CT("leaf_2", {LEAF})}}));

  LeafTrafficClass *leaf_1 = static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_TRUE(leaf_1 != nullptr);
  ASSERT_TRUE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 = static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  ASSERT_TRUE(leaf_2 != nullptr);
  ASSERT_TRUE(leaf_2->blocked());

  EXPECT_EQ(nullptr, s.Next());

  // Adding a fake task should unblock the whole tree.
  leaf_1->AddTask((Task *) 1);
  ASSERT_FALSE(leaf_1->blocked());

  EXPECT_EQ(leaf_1, s.Next());

  EXPECT_TRUE(leaf_1->RemoveTask((Task *) 1));
  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the
// leaves in proportion to their weights.
TEST(ScheduleOnce, TwoLeavesWeightedFair) {
  Scheduler s(CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
              {{WEIGHTED_FAIR, 2, CT("leaf_1", {LEAF})},
               {WEIGHTED_FAIR, 5, CT("leaf_2", {LEAF})}}));

  LeafTrafficClass *leaf_1 = static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_TRUE(leaf_1 != nullptr);
  ASSERT_TRUE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 = static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  ASSERT_TRUE(leaf_2 != nullptr);
  ASSERT_TRUE(leaf_2->blocked());

  EXPECT_EQ(nullptr, s.Next());

  // Unblock both leaves.
  leaf_2->AddTask((Task *) 1);
  ASSERT_FALSE(leaf_2->blocked());
  leaf_1->AddTask((Task *) 1);
  ASSERT_FALSE(leaf_1->blocked());

  // Clear out the tasks so that they don't get called during execution.
  leaf_1->tasks().clear();
  leaf_2->tasks().clear();

  WeightedFairTrafficClass *root = static_cast<WeightedFairTrafficClass *>(s.root());
  ASSERT_EQ(2, root->children().size());

  // There's no guarantee which will run first because they will tie, so this is
  // a guess based upon the heap's behavior.
  ASSERT_EQ(leaf_2, s.Next());
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next());
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next());
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next());
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next());
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next());
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next());

  TrafficClassBuilder::ClearAll();
}


}  // namespace bess
