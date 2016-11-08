// Benchmarks for TC / scheduler.

#include "tc.h"

#include <vector>

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include "utils/time.h"

// Performs TC Scheduler init/deinit before/after each test.
class TCFixture : public benchmark::Fixture {
 public:
  TCFixture() : classes_(), s_() { SetTscHz(); }

  static void SetTscHz() {
    // Compute our own approximate tsc_hz.
    // TODO(barath): Standarize this calculation elsewhere, such as in utils/
    uint64_t start = rdtsc();
    sleep(1);
    tsc_hz = rdtsc() - start;

    CHECK(tsc_hz > 0) << "tsc_hz=" << tsc_hz;
    CHECK((tsc_hz >> 4) > 0) << "tsc_hz>>4=" << (tsc_hz >> 4);
  }

  virtual void SetUp(benchmark::State &state) override {
    int num_classes = state.range(0);

    s_ = sched_init();

    for (int i = 0; i < num_classes; i++) {
      struct tc_params params = {};

      params.name = "class_" + std::to_string(i);
      params.parent = nullptr;
      params.priority = 0;
      params.share = 1;
      params.share_resource = state.range(1);

      struct tc *c = tc_init(s_, &params);
      CHECK(!c->state.queued) << "c->state.queued=" << c->state.queued;
      CHECK(!c->state.runnable) << "c->state.runnable=" << c->state.runnable;

      classes_.push_back(c);
    }

    for (auto c : classes_) {
      tc_join(c);
    }
  }

  virtual void TearDown(benchmark::State &) override {
    // TODO(barath): This leaks memory at the moment, but it's easier than
    // proper cleanup.
    classes_.clear();
    TCContainer::tcs.clear();

    if (s_) {
      sched_free(s_);
      s_ = nullptr;
    }
  }

 protected:
  std::vector<struct tc *> classes_;
  struct sched *s_;
};

// Benchmarks the schedule_once() routine in TC.  For RESOURCE_CNT.
BENCHMARK_DEFINE_F(TCFixture, TCScheduleOnceCount)(benchmark::State &state) {
  while (state.KeepRunning()) {
    // Fake the round value so that we don't go through the stat collection
    // code.
    schedule_once(s_);
  }
  state.SetComplexityN(state.range(0));
}

// Benchmarks the schedule_once() routine in TC.  For RESOURCE_CYCLE.
BENCHMARK_DEFINE_F(TCFixture, TCScheduleOnceCycle)(benchmark::State &state) {
  while (state.KeepRunning()) {
    // Fake the round value so that we don't go through the stat collection
    // code.
    schedule_once(s_);
  }
  state.SetComplexityN(state.range(0));
}

BENCHMARK_REGISTER_F(TCFixture, TCScheduleOnceCount)
    ->Args({4 << 0, RESOURCE_CNT})
    ->Args({4 << 1, RESOURCE_CNT})
    ->Args({4 << 2, RESOURCE_CNT})
    ->Args({4 << 3, RESOURCE_CNT})
    ->Args({4 << 4, RESOURCE_CNT})
    ->Args({4 << 5, RESOURCE_CNT})
    ->Args({4 << 6, RESOURCE_CNT})
    ->Args({4 << 7, RESOURCE_CNT})
    ->Args({4 << 8, RESOURCE_CNT})
    ->Args({4 << 9, RESOURCE_CNT})
    ->Args({4 << 10, RESOURCE_CNT})
    ->Args({4 << 11, RESOURCE_CNT})
    ->Args({4 << 12, RESOURCE_CNT})
    ->Args({4 << 13, RESOURCE_CNT})
    ->Args({4 << 14, RESOURCE_CNT})
    ->Complexity();

BENCHMARK_REGISTER_F(TCFixture, TCScheduleOnceCycle)
    ->Args({4 << 0, RESOURCE_CYCLE})
    ->Args({4 << 1, RESOURCE_CYCLE})
    ->Args({4 << 2, RESOURCE_CYCLE})
    ->Args({4 << 3, RESOURCE_CYCLE})
    ->Args({4 << 4, RESOURCE_CYCLE})
    ->Args({4 << 5, RESOURCE_CYCLE})
    ->Args({4 << 6, RESOURCE_CYCLE})
    ->Args({4 << 7, RESOURCE_CYCLE})
    ->Args({4 << 8, RESOURCE_CYCLE})
    ->Args({4 << 9, RESOURCE_CYCLE})
    ->Args({4 << 10, RESOURCE_CYCLE})
    ->Args({4 << 11, RESOURCE_CYCLE})
    ->Args({4 << 12, RESOURCE_CYCLE})
    ->Args({4 << 13, RESOURCE_CYCLE})
    ->Args({4 << 14, RESOURCE_CYCLE})
    ->Complexity();

BENCHMARK_MAIN();
