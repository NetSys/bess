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

// Benchmarks for TC / scheduler.

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include <vector>

#include "module.h"

#include "scheduler.h"
#include "traffic_class.h"

#define CT TrafficClassBuilder::CreateTree

using namespace bess;
using namespace bess::traffic_class_initializer_types;

namespace {

class DummyModule : public Module {
 public:
  struct task_result RunTask(Context *, bess::PacketBatch *,
                             void *arg) override;
};

[[gnu::noinline]] struct task_result DummyModule::RunTask(Context *,
                                                          bess::PacketBatch *,
                                                          void *) {
  return {.block = false, .packets = 0, .bits = 0};
}

// Performs TC Scheduler init/deinit before/after each test.
// Sets up a tree for weighted fair benchmarking.
class TCWeightedFair : public benchmark::Fixture {
 public:
  TCWeightedFair() : s_(), dummy_() {}

  void SetUp(benchmark::State &state) override {
    int num_classes = state.range(0);
    resource_t resource = (resource_t)state.range(1);

    dummy_ = new DummyModule;

    TrafficClass *root =
        CT("root", {PRIORITY},
           {{0, CT("weighted", {WEIGHTED_FAIR, resource}, {})}});
    s_ = new DefaultScheduler(root);
    WeightedFairTrafficClass *weighted =
        static_cast<WeightedFairTrafficClass *>(
            TrafficClassBuilder::Find("weighted"));
    for (int i = 0; i < num_classes; i++) {
      std::string name("class_" + std::to_string(i));
      LeafTrafficClass *c =
          TrafficClassBuilder::CreateTrafficClass<LeafTrafficClass>(
              name, new Task(dummy_, nullptr));

      resource_share_t share = 1;
      CHECK(weighted->AddChild(c, share));
    }
    CHECK(!root->blocked());
    CHECK(!weighted->blocked());
  }

  void TearDown(benchmark::State &) override {
    delete s_;
    s_ = nullptr;

    delete dummy_;
    dummy_ = nullptr;

    TrafficClassBuilder::ClearAll();
  }

 protected:
  DefaultScheduler *s_;
  Module *dummy_;
};

// Benchmarks the schedule_once() routine in TC.  For RESOURCE_CNT.
BENCHMARK_DEFINE_F(TCWeightedFair, TCScheduleOnceCount)
(benchmark::State &state) {
  while (state.KeepRunning()) {
    Context ctx = {};
    s_->ScheduleOnce(&ctx);
  }
  state.SetItemsProcessed(state.iterations());
  state.SetComplexityN(state.range(0));
}

// Benchmarks the schedule_once() routine in TC.  For RESOURCE_CYCLE.
BENCHMARK_DEFINE_F(TCWeightedFair, TCScheduleOnceCycle)
(benchmark::State &state) {
  while (state.KeepRunning()) {
    Context ctx = {};
    s_->ScheduleOnce(&ctx);
  }
  state.SetItemsProcessed(state.iterations());
  state.SetComplexityN(state.range(0));
}

BENCHMARK_REGISTER_F(TCWeightedFair, TCScheduleOnceCount)
    ->Args({4 << 0, bess::RESOURCE_COUNT})
    ->Args({4 << 1, bess::RESOURCE_COUNT})
    ->Args({4 << 2, bess::RESOURCE_COUNT})
    ->Args({4 << 3, bess::RESOURCE_COUNT})
    ->Args({4 << 4, bess::RESOURCE_COUNT})
    ->Args({4 << 5, bess::RESOURCE_COUNT})
    ->Args({4 << 6, bess::RESOURCE_COUNT})
    ->Args({4 << 7, bess::RESOURCE_COUNT})
    ->Args({4 << 8, bess::RESOURCE_COUNT})
    ->Args({4 << 9, bess::RESOURCE_COUNT})
    ->Args({4 << 10, bess::RESOURCE_COUNT})
    ->Args({4 << 11, bess::RESOURCE_COUNT})
    ->Args({4 << 12, bess::RESOURCE_COUNT})
    ->Args({4 << 13, bess::RESOURCE_COUNT})
    ->Args({4 << 14, bess::RESOURCE_COUNT})
    ->Complexity();

BENCHMARK_REGISTER_F(TCWeightedFair, TCScheduleOnceCycle)
    ->Args({4 << 0, bess::RESOURCE_CYCLE})
    ->Args({4 << 1, bess::RESOURCE_CYCLE})
    ->Args({4 << 2, bess::RESOURCE_CYCLE})
    ->Args({4 << 3, bess::RESOURCE_CYCLE})
    ->Args({4 << 4, bess::RESOURCE_CYCLE})
    ->Args({4 << 5, bess::RESOURCE_CYCLE})
    ->Args({4 << 6, bess::RESOURCE_CYCLE})
    ->Args({4 << 7, bess::RESOURCE_CYCLE})
    ->Args({4 << 8, bess::RESOURCE_CYCLE})
    ->Args({4 << 9, bess::RESOURCE_CYCLE})
    ->Args({4 << 10, bess::RESOURCE_CYCLE})
    ->Args({4 << 11, bess::RESOURCE_CYCLE})
    ->Args({4 << 12, bess::RESOURCE_CYCLE})
    ->Args({4 << 13, bess::RESOURCE_CYCLE})
    ->Args({4 << 14, bess::RESOURCE_CYCLE})
    ->Complexity();

// Performs TC Scheduler init/deinit before/after each test.
class TCRoundRobin : public benchmark::Fixture {
 public:
  TCRoundRobin() : s_(), dummy_() {}

  void SetUp(benchmark::State &state) override {
    int num_classes = state.range(0);

    dummy_ = new DummyModule;

    TrafficClass *root = CT("rr", {ROUND_ROBIN}, {});
    s_ = new DefaultScheduler(root);
    RoundRobinTrafficClass *rr =
        static_cast<RoundRobinTrafficClass *>(TrafficClassBuilder::Find("rr"));

    for (int i = 0; i < num_classes; i++) {
      std::string name("class_" + std::to_string(i));
      LeafTrafficClass *c =
          TrafficClassBuilder::CreateTrafficClass<LeafTrafficClass>(
              name, new Task(dummy_, nullptr));

      CHECK(rr->AddChild(c));
    }
    CHECK(!rr->blocked());
  }

  void TearDown(benchmark::State &) override {
    delete s_;
    s_ = nullptr;

    delete dummy_;
    dummy_ = nullptr;

    TrafficClassBuilder::ClearAll();
  }

 protected:
  DefaultScheduler *s_;
  Module *dummy_;
};

BENCHMARK_DEFINE_F(TCRoundRobin, TCScheduleOnce)(benchmark::State &state) {
  while (state.KeepRunning()) {
    Context ctx = {};
    s_->ScheduleOnce(&ctx);
  }
  state.SetItemsProcessed(state.iterations());
  state.SetComplexityN(state.range(0));
}

BENCHMARK_REGISTER_F(TCRoundRobin, TCScheduleOnce)
    ->Args({1 << 0})
    ->Args({4 << 0})
    ->Args({4 << 1})
    ->Args({4 << 2})
    ->Args({4 << 3})
    ->Args({4 << 4})
    ->Args({4 << 5})
    ->Args({4 << 6})
    ->Args({4 << 7})
    ->Args({4 << 8})
    ->Args({4 << 9})
    ->Args({4 << 10})
    ->Args({4 << 11})
    ->Args({4 << 12})
    ->Args({4 << 13})
    ->Args({4 << 14})
    ->Complexity();

}  // namespace

BENCHMARK_MAIN();
