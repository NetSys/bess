// Benchmarks for TC / scheduler.

#include "scheduler.h"
#include "traffic_class.h"

#include <vector>

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#define CT TrafficClassBuilder::CreateTree

using namespace bess;
using namespace bess::traffic_class_initializer_types;

namespace {

// Performs TC Scheduler init/deinit before/after each test.
class TCFixture : public benchmark::Fixture {
 public:
  TCFixture() : s_() {}

  virtual void SetUp(benchmark::State &state) override {
    int num_classes = state.range(0);
    resource_t resource = (resource_t) state.range(1);

    TrafficClass *root = CT("root", {PRIORITY},
                            {{PRIORITY, 0, CT("weighted", {WEIGHTED_FAIR, resource}, {})}});
    s_ = new Scheduler(root);
    WeightedFairTrafficClass *weighted = static_cast<WeightedFairTrafficClass *>(TrafficClassBuilder::Find("weighted"));
    for (int i = 0; i < num_classes; i++) {
      std::string name("class_" + std::to_string(i));
      LeafTrafficClass *c = new LeafTrafficClass(name);
      c->AddTask((Task *) 1);  // A fake task.

      resource_share_t share = 1;
      CHECK(weighted->AddChild(c, share));
      c->tasks().clear();
    }
    CHECK(!root->blocked());
    CHECK(!weighted->blocked());
  }

  virtual void TearDown(benchmark::State &) override {
    delete s_;
    s_ = nullptr;

    TrafficClassBuilder::ClearAll();
  }

 protected:
  Scheduler *s_;
};

}  // namespace

// Benchmarks the schedule_once() routine in TC.  For RESOURCE_CNT.
BENCHMARK_DEFINE_F(TCFixture, TCScheduleOnceCount)(benchmark::State &state) {
  while (state.KeepRunning()) {
    s_->ScheduleOnce();
  }
  state.SetItemsProcessed(state.iterations());
  state.SetComplexityN(state.range(0));
}

// Benchmarks the schedule_once() routine in TC.  For RESOURCE_CYCLE.
BENCHMARK_DEFINE_F(TCFixture, TCScheduleOnceCycle)(benchmark::State &state) {
  while (state.KeepRunning()) {
    s_->ScheduleOnce();
  }
  state.SetItemsProcessed(state.iterations());
  state.SetComplexityN(state.range(0));
}

BENCHMARK_REGISTER_F(TCFixture, TCScheduleOnceCount)
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

BENCHMARK_REGISTER_F(TCFixture, TCScheduleOnceCycle)
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

BENCHMARK_MAIN();
