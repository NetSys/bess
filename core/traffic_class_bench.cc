// Benchmarks for TC / scheduler.

#include "scheduler.h"
#include "traffic_class.h"

#include <vector>

#include <benchmark/benchmark.h>
#include <glog/logging.h>

using bess::Scheduler;
using bess::TrafficClass;

using bess::LeafTrafficClass;
using bess::PriorityTrafficClass;
using bess::WeightedFairTrafficClass;

using bess::resource_t;
using bess::resource_share_t;

namespace {

// Performs TC Scheduler init/deinit before/after each test.
class TCFixture : public benchmark::Fixture {
 public:
  TCFixture() : classes_(), s_() {}

  virtual void SetUp(benchmark::State &state) override {
    int num_classes = state.range(0);
    resource_t resource = (resource_t) state.range(1);

    s_ = new Scheduler();
    PriorityTrafficClass *pc = s_->root();

    // The main weighted traffic class we attach everything to.
    WeightedFairTrafficClass *wc = new WeightedFairTrafficClass("weighted", resource);
    pc->AddChild(wc, 0);
    classes_.push_back(wc);

    for (int i = 0; i < num_classes; i++) {
      std::string name("class_" + std::to_string(i));
      LeafTrafficClass *c = new LeafTrafficClass(name);

      resource_share_t share = 1;
      wc->AddChild(c, share);

      classes_.push_back(c);
    }
  }

  virtual void TearDown(benchmark::State &) override {
    for (TrafficClass *c : classes_) {
      delete c;
    }
    classes_.clear();

    delete s_;
    s_ = nullptr;
  }

 protected:
  std::vector<TrafficClass *> classes_;
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
