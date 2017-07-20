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

// Benchmarks for our custom hashtable implementation.
//
// TODO(barath): Add dpdk benchmarks from oldtests/htable.cc once we re-enable
// dpdk memory allocation.

#include "cuckoo_map.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <unordered_map>

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include "common.h"
#include "random.h"

using bess::utils::CuckooMap;

typedef uint16_t value_t;

static inline value_t derive_val(uint32_t key) {
  return (value_t)(key + 3);
}

static Random rng;

// Performs CuckooMap setup / teardown.
class CuckooMapFixture : public benchmark::Fixture {
 public:
  CuckooMapFixture() : cuckoo_(), stl_map_() {}

  virtual void SetUp(benchmark::State &state) {
    stl_map_ = new std::unordered_map<uint32_t, value_t>();
    cuckoo_ = new CuckooMap<uint32_t, value_t>();

    rng.SetSeed(0);

    for (int i = 0; i < state.range(0); i++) {
      uint32_t key = rng.Get();
      value_t val = derive_val(key);

      (*stl_map_)[key] = val;
      cuckoo_->Insert(key, val);
    }
  }

  virtual void TearDown(benchmark::State &) {
    delete stl_map_;
    delete cuckoo_;
  }

 protected:
  CuckooMap<uint32_t, value_t> *cuckoo_;
  std::unordered_map<uint32_t, value_t> *stl_map_;
};

// Benchmarks the Find() method in CuckooMap, which is inlined.
BENCHMARK_DEFINE_F(CuckooMapFixture, CuckooMapInlinedGet)
(benchmark::State &state) {
  while (true) {
    const size_t n = state.range(0);
    rng.SetSeed(0);

    for (size_t i = 0; i < n; i++) {
      uint32_t key = rng.Get();
      std::pair<uint32_t, value_t> *val;

      benchmark::DoNotOptimize(val = cuckoo_->Find(key));
      DCHECK(val);
      DCHECK_EQ(val->second, derive_val(key));

      if (!state.KeepRunning()) {
        state.SetItemsProcessed(state.iterations());
        return;
      }
    }
  }
}

BENCHMARK_REGISTER_F(CuckooMapFixture, CuckooMapInlinedGet)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 20);

// Benchmarks the find method on the STL unordered_map.
BENCHMARK_DEFINE_F(CuckooMapFixture, STLUnorderedMapGet)
(benchmark::State &state) {
  while (true) {
    const size_t n = state.range(0);
    rng.SetSeed(0);

    for (size_t i = 0; i < n; i++) {
      uint32_t key = rng.Get();
      value_t val;

      benchmark::DoNotOptimize(val = (*stl_map_)[key]);
      DCHECK_EQ(val, derive_val(key));

      if (!state.KeepRunning()) {
        state.SetItemsProcessed(state.iterations());
        return;
      }
    }
  }
}

BENCHMARK_REGISTER_F(CuckooMapFixture, STLUnorderedMapGet)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 20);

BENCHMARK_MAIN();
