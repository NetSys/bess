// Benchmark for UrlFilter module.

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include "url_filter.h"

// Benchmarks the NAT flow hash.
static void BM_FlowHash(benchmark::State& state) {
  Flow f;
  FlowHash h;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(h(f));
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_FlowHash);

BENCHMARK_MAIN();
