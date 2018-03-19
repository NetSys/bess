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

// Unit tests traffic class and scheduler routines.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "module.h"
#include "scheduler.h"
#include "traffic_class.h"

#define CT TrafficClassBuilder::CreateTree

using namespace bess::traffic_class_initializer_types;

namespace bess {

class DummyModule : public Module {
 public:
  struct task_result RunTask(const Task *, bess::PacketBatch *,
                             void *arg) override;
};

[[gnu::noinline]] struct task_result DummyModule::RunTask(const Task *,
                                                          bess::PacketBatch *,
                                                          void *) {
  return {.block = false, .packets = 0, .bits = 0};
}

// Tests that we can create a leaf node.
TEST(CreateTree, Leaf) {
  std::unique_ptr<TrafficClass> c(
      CT("leaf", {LEAF, new Task(nullptr, nullptr)}));
  ASSERT_NE(nullptr, c.get());
  ASSERT_EQ(1, c->Size());
  EXPECT_EQ(POLICY_LEAF, c->policy());

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a priority root node with a leaf under it.
TEST(CreateTree, PriorityRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(
      CT("root", {PRIORITY},
         {{10, CT("leaf", {LEAF, new Task(nullptr, nullptr)})}}));
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
  TrafficClass *leaf2 = CT("leaf_2", {LEAF, new Task(nullptr, nullptr)});
  ASSERT_FALSE(c->AddChild(leaf2, 10));

  // We shouldn't be able to remove a child that does not exist.
  ASSERT_FALSE(c->RemoveChild(leaf2));
  delete leaf2;

  // A different priority should be fine.
  TrafficClass *leaf3 = CT("leaf_3", {LEAF, new Task(nullptr, nullptr)});
  ASSERT_TRUE(c->AddChild(leaf3, 2));
  ASSERT_EQ(1, TrafficClassBuilder::Find("leaf_3")->Size());
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  ASSERT_TRUE(c->RemoveChild(leaf3));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());
  delete leaf3;

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a weighted fair root node with a leaf
// under it.
TEST(CreateTree, WeightedFairRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(
      CT("root", {WEIGHTED_FAIR, RESOURCE_CYCLE},
         {{10, CT("leaf", {LEAF, new Task(nullptr, nullptr)})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, tree);
  EXPECT_EQ(POLICY_WEIGHTED_FAIR, tree->policy());

  WeightedFairTrafficClass *c =
      static_cast<WeightedFairTrafficClass *>(tree.get());
  ASSERT_NE(nullptr, c);
  EXPECT_EQ(RESOURCE_CYCLE, c->resource());
  ASSERT_EQ(1, c->runnable_children().size());
  ASSERT_EQ(0, c->blocked_children().size());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(
      c->runnable_children().container().front().c);
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClass *leaf2 = CT("leaf_2", {LEAF, new Task(nullptr, nullptr)});
  ASSERT_TRUE(c->AddChild(leaf2, 2));
  ASSERT_EQ(1, TrafficClassBuilder::Find("leaf_2")->Size());
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  ASSERT_TRUE(c->RemoveChild(leaf2));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());
  delete leaf2;

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a round robin root node with a leaf under
// it.
TEST(CreateTree, RoundRobinRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(
      CT("root", {ROUND_ROBIN},
         {{CT("leaf", {LEAF, new Task(nullptr, nullptr)})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, tree);
  EXPECT_EQ(POLICY_ROUND_ROBIN, tree->policy());

  RoundRobinTrafficClass *c = static_cast<RoundRobinTrafficClass *>(tree.get());
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(1, c->runnable_children().size());
  ASSERT_EQ(0, c->blocked_children().size());

  LeafTrafficClass *leaf =
      static_cast<LeafTrafficClass *>(c->runnable_children().front());
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  TrafficClass *leaf2 = CT("leaf_2", {LEAF, new Task(nullptr, nullptr)});
  ASSERT_TRUE(c->AddChild(leaf2));
  ASSERT_EQ(1, TrafficClassBuilder::Find("leaf_2")->Size());
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  ASSERT_TRUE(c->RemoveChild(leaf2));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());
  delete leaf2;

  TrafficClassBuilder::ClearAll();
}

// Tests that we can create and fetch a rate limit root node with a leaf under
// it.
TEST(CreateTree, RateLimitRootAndLeaf) {
  std::unique_ptr<TrafficClass> tree(
      CT("root", {RATE_LIMIT, RESOURCE_CYCLE, 10, 15},
         {CT("leaf", {LEAF, new Task(nullptr, nullptr)})}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, tree);
  EXPECT_EQ(POLICY_RATE_LIMIT, tree->policy());

  RateLimitTrafficClass *c = static_cast<RateLimitTrafficClass *>(tree.get());
  ASSERT_NE(nullptr, c);
  EXPECT_EQ(RESOURCE_CYCLE, c->resource());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->child());
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  ASSERT_TRUE(c->RemoveChild(leaf));
  ASSERT_EQ(1, TrafficClassBuilder::Find("root")->Size());
  delete leaf;

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(DefaultSchedulerNext, BasicTreePriority) {
  DefaultScheduler s(
      CT("root", {PRIORITY},
         {{10, CT("leaf", {LEAF, new Task(nullptr, nullptr)})}}));
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

  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(DefaultSchedulerNext, BasicTreeWeightedFair) {
  DefaultScheduler s(CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
                        {{2, CT("leaf", {LEAF, new Task(nullptr, nullptr)})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, s.root());
  EXPECT_EQ(POLICY_WEIGHTED_FAIR, s.root()->policy());

  WeightedFairTrafficClass *c =
      static_cast<WeightedFairTrafficClass *>(s.root());
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(1, c->runnable_children().size());
  ASSERT_EQ(0, c->blocked_children().size());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(
      c->runnable_children().container().front().c);
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(DefaultSchedulerNext, BasicTreeRoundRobin) {
  DefaultScheduler s(CT("root", {ROUND_ROBIN},
                        {{CT("leaf", {LEAF, new Task(nullptr, nullptr)})}}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, s.root());
  EXPECT_EQ(POLICY_ROUND_ROBIN, s.root()->policy());

  RoundRobinTrafficClass *c = static_cast<RoundRobinTrafficClass *>(s.root());
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(0, c->blocked_children().size());

  LeafTrafficClass *leaf =

      static_cast<LeafTrafficClass *>(c->runnable_children().front());
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the leaf
// repeatedly.
TEST(DefaultSchedulerNext, BasicTreeRateLimit) {
  uint64_t new_limit = 25;
  uint64_t new_burst = 50;
  DefaultScheduler s(CT("root", {RATE_LIMIT, RESOURCE_COUNT, 50, 100},
                        {CT("leaf", {LEAF, new Task(nullptr, nullptr)})}));
  ASSERT_EQ(2, TrafficClassBuilder::Find("root")->Size());

  ASSERT_NE(nullptr, s.root());
  EXPECT_EQ(POLICY_RATE_LIMIT, s.root()->policy());

  RateLimitTrafficClass *c = static_cast<RateLimitTrafficClass *>(s.root());
  ASSERT_NE(nullptr, c);

  ASSERT_EQ(RESOURCE_COUNT, c->resource());
  ASSERT_EQ(50, c->limit_arg());
  ASSERT_EQ(RateLimitTrafficClass::to_work_units_per_cycle(50), c->limit());
  ASSERT_EQ(100, c->max_burst_arg());
  ASSERT_EQ(RateLimitTrafficClass::to_work_units(100), c->max_burst());

  c->set_resource(RESOURCE_PACKET);
  c->set_limit(new_limit);
  c->set_max_burst(new_burst);

  ASSERT_EQ(RESOURCE_PACKET, c->resource());
  ASSERT_EQ(new_limit, c->limit_arg());
  ASSERT_EQ(RateLimitTrafficClass::to_work_units_per_cycle(new_limit),
            c->limit());
  ASSERT_EQ(new_burst, c->max_burst_arg());
  ASSERT_EQ(RateLimitTrafficClass::to_work_units(new_burst), c->max_burst());

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(c->child());
  ASSERT_NE(nullptr, leaf);
  EXPECT_EQ(leaf->parent(), c);

  ASSERT_FALSE(leaf->blocked());

  EXPECT_EQ(leaf, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the
// unblocked child repeatedly if one of the children is blocked.
TEST(DefaultSchedulerNext, TwoLeavesWeightedFairOneBlocked) {
  DefaultScheduler s(
      CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
         {{1, CT("rr_1", {ROUND_ROBIN})}, {2, CT("rr_2", {ROUND_ROBIN})}}));
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  RoundRobinTrafficClass *rr_1 =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr_1"));
  ASSERT_NE(nullptr, rr_1);
  ASSERT_TRUE(rr_1->blocked());

  RoundRobinTrafficClass *rr_2 =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr_2"));
  ASSERT_NE(nullptr, rr_2);
  ASSERT_TRUE(rr_2->blocked());

  EXPECT_EQ(nullptr, s.Next(rdtsc()));

  LeafTrafficClass *leaf_1 = static_cast<LeafTrafficClass *>(
      CT("leaf_1", {LEAF, new Task(nullptr, nullptr)}));
  rr_1->AddChild(leaf_1);

  ASSERT_FALSE(rr_1->blocked());

  EXPECT_EQ(leaf_1, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the
// leaves in proportion to their weights.
TEST(DefaultScheduleOnce, TwoLeavesWeightedFair) {
  DummyModule dm;
  DefaultScheduler s(CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
                        {{5, CT("leaf_2", {LEAF, new Task(&dm, nullptr)})},
                         {2, CT("leaf_1", {LEAF, new Task(&dm, nullptr)})}}));
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  LeafTrafficClass *leaf_1 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_NE(nullptr, leaf_1);
  ASSERT_FALSE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  ASSERT_NE(nullptr, leaf_2);
  ASSERT_FALSE(leaf_2->blocked());

  WeightedFairTrafficClass *root =
      static_cast<WeightedFairTrafficClass *>(s.root());
  ASSERT_EQ(2, root->runnable_children().size());

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
TEST(DefaultScheduleOnce, TwoLeavesPriority) {
  DummyModule dm;
  DefaultScheduler s(CT("root", {PRIORITY}, {{0, CT("rr_1", {ROUND_ROBIN})},
                                             {1, CT("rr_2", {ROUND_ROBIN})}}));
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  RoundRobinTrafficClass *rr_1 =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr_1"));
  ASSERT_NE(nullptr, rr_1);
  ASSERT_TRUE(rr_1->blocked());

  RoundRobinTrafficClass *rr_2 =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr_2"));
  ASSERT_NE(nullptr, rr_2);
  ASSERT_TRUE(rr_2->blocked());

  ASSERT_EQ(nullptr, s.Next(rdtsc()));

  // Unblock the second rr
  LeafTrafficClass *leaf_2 = static_cast<LeafTrafficClass *>(
      CT("leaf_2", {LEAF, new Task(&dm, nullptr)}));
  rr_2->AddChild(leaf_2);
  ASSERT_FALSE(rr_2->blocked());

  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_2, s.Next(rdtsc()));

  // Unblock the first rr, which should now get picked.
  LeafTrafficClass *leaf_1 = static_cast<LeafTrafficClass *>(
      CT("leaf_1", {LEAF, new Task(&dm, nullptr)}));
  rr_1->AddChild(leaf_1);
  ASSERT_FALSE(rr_1->blocked());

  ASSERT_EQ(leaf_1, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next(rdtsc()));
  s.ScheduleOnce();
  ASSERT_EQ(leaf_1, s.Next(rdtsc()));

  TrafficClassBuilder::ClearAll();
}

// Tess that we can create a simple tree and have the scheduler pick the
// leaves round robin.
TEST(DefaultScheduleOnce, TwoLeavesRoundRobin) {
  DummyModule dm;
  DefaultScheduler s(CT("root", {ROUND_ROBIN},
                        {{CT("leaf_1", {LEAF, new Task(&dm, nullptr)})},
                         {CT("leaf_2", {LEAF, new Task(&dm, nullptr)})}}));
  ASSERT_EQ(3, TrafficClassBuilder::Find("root")->Size());

  LeafTrafficClass *leaf_1 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_NE(nullptr, leaf_1);
  ASSERT_FALSE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
  ASSERT_NE(nullptr, leaf_2);
  ASSERT_FALSE(leaf_2->blocked());

  RoundRobinTrafficClass *root =
      static_cast<RoundRobinTrafficClass *>(s.root());
  ASSERT_EQ(2, root->runnable_children().size());

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
TEST(DefaultScheduleOnce, LeavesWeightedFairAndRoundRobin) {
  DummyModule dm;
  DefaultScheduler s(
      CT("root", {WEIGHTED_FAIR, RESOURCE_COUNT},
         {{2, CT("rr_1", {ROUND_ROBIN},
                 {{CT("leaf_1a", {LEAF, new Task(&dm, nullptr)})},
                  {CT("leaf_1b", {LEAF, new Task(&dm, nullptr)})}})},
          {5, CT("rr_2", {ROUND_ROBIN},
                 {
                     {CT("leaf_2a", {LEAF, new Task(&dm, nullptr)})},
                     {CT("leaf_2b", {LEAF, new Task(&dm, nullptr)})},
                 })}}));
  ASSERT_EQ(7, TrafficClassBuilder::Find("root")->Size());

  std::map<std::string, LeafTrafficClass *> leaves;
  for (auto &leaf_name : {"leaf_1a", "leaf_1b", "leaf_2a", "leaf_2b"}) {
    LeafTrafficClass *leaf =
        static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find(leaf_name));
    ASSERT_NE(nullptr, leaf);
    leaves[leaf_name] = leaf;

    ASSERT_FALSE(leaf->blocked());
  }

  WeightedFairTrafficClass *root =
      static_cast<WeightedFairTrafficClass *>(s.root());
  ASSERT_EQ(2, root->runnable_children().size());
  RoundRobinTrafficClass *rr_1 =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr_1"));
  ASSERT_EQ(2, rr_1->runnable_children().size());
  RoundRobinTrafficClass *rr_2 =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr_2"));
  ASSERT_EQ(2, rr_2->runnable_children().size());

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
  DefaultScheduler s(
      CT("root", {ROUND_ROBIN},
         {{CT("limit_1", {RATE_LIMIT, RESOURCE_COUNT, 1, 0},
              {CT("leaf_1", {LEAF, new Task(nullptr, nullptr)})})},
          {CT("limit_2", {RATE_LIMIT, RESOURCE_COUNT, 1, 0},
              {CT("leaf_2", {LEAF, new Task(nullptr, nullptr)})})}}));
  ASSERT_EQ(5, TrafficClassBuilder::Find("root")->Size());
  RoundRobinTrafficClass *rr =
      static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("root"));
  ASSERT_NE(nullptr, rr);

  LeafTrafficClass *leaf_1 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_1"));
  ASSERT_FALSE(leaf_1->blocked());

  LeafTrafficClass *leaf_2 =
      static_cast<LeafTrafficClass *>(TrafficClassBuilder::Find("leaf_2"));
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
  c->FinishAndAccountTowardsRoot(&s.wakeup_queue(), nullptr, usage, now);
  ASSERT_TRUE(limit_1->blocked());

  // Fake quarter second delay, schedule again.
  now += tsc_hz / 4;
  c = s.Next(now);
  ASSERT_EQ(c, leaf_2);
  c->FinishAndAccountTowardsRoot(&s.wakeup_queue(), nullptr, usage, now);
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
      << "tsc=" << now << ", limit_1 expiration=" << limit_1->wakeup_time();
  ASSERT_FALSE(limit_2->blocked())
      << "tsc=" << now << ", limit_2 expiration=" << limit_2->wakeup_time();
  ASSERT_FALSE(rr->blocked());

  EXPECT_EQ(c, leaf_1);
  c->FinishAndAccountTowardsRoot(&s.wakeup_queue(), nullptr, usage, now);
  now += tsc_hz / 4;
  c = s.Next(now);
  ASSERT_EQ(c, leaf_2);

  TrafficClassBuilder::ClearAll();
}

}  // namespace bess
