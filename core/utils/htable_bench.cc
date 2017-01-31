// Benchmarks for our custom hashtable implementation.
//
// TODO(barath): Add dpdk benchmarks from oldtests/htable.cc once we re-enable
// dpdk memory allocation.

#include "cuckoo_map.h"
#include "htable.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <unordered_map>

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include <rte_config.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>

#include "../mem_alloc.h"
#include "common.h"
#include "random.h"

using bess::utils::HTable;
using bess::utils::HTableBase;
using bess::utils::CuckooMap;

typedef uint16_t value_t;

static inline int inlined_keycmp(const void *key, const void *key_stored,
                                 size_t) {
  return *(uint32_t *)key != *(uint32_t *)key_stored;
}

static inline uint32_t inlined_hash(const void *key, uint32_t,
                                    uint32_t init_val) {
#if __SSE4_2__
  return crc32c_sse42_u32(*(uint32_t *)key, init_val);
#else
  return rte_hash_crc_4byte(*(uint32_t *)key, init_val);
#endif
}

static inline value_t derive_val(uint32_t key) {
  return (value_t)(key + 3);
}

static Random rng;

// Performs HTable setup / teardown.
class HTableFixture : public benchmark::Fixture {
 public:
  HTableFixture() : arg_(), stl_map_() {}

  virtual void SetUp(benchmark::State &state) {
    HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *t =
        new HTable<uint32_t, value_t, inlined_keycmp, inlined_hash>();
    stl_map_ = new std::unordered_map<uint32_t, value_t>();
    cuckoo_ = new CuckooMap<uint32_t, value_t>();

    if (t->Init(sizeof(uint32_t), sizeof(value_t)) == -ENOMEM) {
      CHECK(false) << "Out of memory.";
    }
    rng.SetSeed(0);

    for (int i = 0; i < state.range(0); i++) {
      uint32_t key = rng.Get();
      value_t val = derive_val(key);

      int ret = t->Set(&key, &val);
      if (ret == -ENOMEM) {
        CHECK(ret != -ENOMEM);
      } else {
        CHECK(ret == 0 || ret == 1);
      }

      (*stl_map_)[key] = val;
      cuckoo_->Insert(key, val);
    }

    arg_ = static_cast<HTableBase *>(t);
  }

  virtual void TearDown(benchmark::State &) {
    HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *t =
        (HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *)arg_;
    t->Close();

    delete stl_map_;
    delete cuckoo_;
  }

 protected:
  HTableBase *arg_;
  CuckooMap<uint32_t, value_t> *cuckoo_;
  std::unordered_map<uint32_t, value_t> *stl_map_;
};

// Benchmarks the Get() method in HTableBase.
BENCHMARK_DEFINE_F(HTableFixture, BessGet)(benchmark::State &state) {
  HTableBase *t = static_cast<HTableBase *>(arg_);

  while (true) {
    const size_t n = state.range(0);
    rng.SetSeed(0);

    for (size_t i = 0; i < n; i++) {
      uint32_t key = rng.Get();
      value_t *val;

      benchmark::DoNotOptimize(val = (value_t *)t->Get(&key));
      DCHECK(val);
      DCHECK_EQ(*val, derive_val(key));

      if (!state.KeepRunning()) {
        state.SetItemsProcessed(state.iterations());
        return;
      }
    }
  }
}

BENCHMARK_REGISTER_F(HTableFixture, BessGet)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 20);

// Benchmarks the Get() method in HTable, which is inlined.
BENCHMARK_DEFINE_F(HTableFixture, BessInlinedGet)(benchmark::State &state) {
  HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *t =
      (HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *)arg_;

  while (true) {
    const size_t n = state.range(0);
    rng.SetSeed(0);

    for (size_t i = 0; i < n; i++) {
      uint32_t key = rng.Get();
      value_t *val;

      benchmark::DoNotOptimize(val = (value_t *)t->Get(&key));
      DCHECK(val);
      DCHECK_EQ(*val, derive_val(key));

      if (!state.KeepRunning()) {
        state.SetItemsProcessed(state.iterations());
        return;
      }
    }
  }
}

BENCHMARK_REGISTER_F(HTableFixture, BessInlinedGet)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 20);

// Benchmarks the Find() method in CuckooMap, which is inlined.
BENCHMARK_DEFINE_F(HTableFixture, CuckooMapInlinedGet)
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

BENCHMARK_REGISTER_F(HTableFixture, CuckooMapInlinedGet)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 20);

// Benchmarks the find method on the STL unordered_map.
BENCHMARK_DEFINE_F(HTableFixture, STLUnorderedMapGet)(benchmark::State &state) {
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

BENCHMARK_REGISTER_F(HTableFixture, STLUnorderedMapGet)
    ->RangeMultiplier(4)
    ->Range(4, 4 << 20);

BENCHMARK_MAIN();
