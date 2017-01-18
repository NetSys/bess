// Benchmark for NAT module.

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include "nat.h"

// Benchmarks the NAT flow hash.
static void BM_FlowHash(benchmark::State& state) {
  Flow f;
  FlowHash h;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(h(f));
  }
}

BENCHMARK(BM_FlowHash);

BENCHMARK_MAIN();
