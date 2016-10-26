// Benchmarks for our custom hashtable implementation.
//
// TODO(barath): Add dpdk benchmarks from oldtests/htable.cc once we re-enable
// dpdk memory allocation.

#include <assert.h>
#include <functional>
#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include <rte_config.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>

#include "../snbuf.h"

#include "../utils/htable.h"
#include "../utils/random.h"

#include "../common.h"
#include "../mem_alloc.h"

typedef uint16_t value_t;

static inline int inlined_keycmp(const void *key, const void *key_stored,
                                 size_t key_size) {
  return *(uint32_t *)key != *(uint32_t *)key_stored;
}

static inline uint32_t inlined_hash(const void *key, uint32_t key_size,
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

// Performs BESS init/close before/after each test.
class BessFixture : public benchmark::Fixture {
 public:
  BessFixture() : arg_() {}

  virtual void SetUp(benchmark::State &state) {
    HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *t =
        new HTable<uint32_t, value_t, inlined_keycmp, inlined_hash>();

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
    }

    arg_ = static_cast<HTableBase *>(t);
  }

  virtual void TearDown(benchmark::State &st) {
    HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *t =
        (HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *) arg_;
    t->Close();
  }

 protected:
  HTableBase *arg_;
};

// Benchmarks the Get() method in HTableBase.
BENCHMARK_DEFINE_F(BessFixture, BessGet)(benchmark::State& state) {
  HTableBase *t = static_cast<HTableBase *>(arg_);

  while (state.KeepRunning()) {
    rng.SetSeed(0);
    for (int i = 0; i < state.range(0); i++) {
      uint32_t key = rng.Get();
      value_t *val;

      val = (value_t *)t->Get(&key);
      assert(val && *val == derive_val(key));
    }
  }
}

BENCHMARK_REGISTER_F(BessFixture, BessGet)->RangeMultiplier(4)->Range(4, 4<<20);

// Benchmarks the Get() method in HTable, which is inlined.
BENCHMARK_DEFINE_F(BessFixture, BessInlinedGet)(benchmark::State& state) {
  HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *t =
      (HTable<uint32_t, value_t, inlined_keycmp, inlined_hash> *)arg_;

  while (state.KeepRunning()) {
    rng.SetSeed(0);
    for (int i = 0; i < state.range(0); i++) {
      uint32_t key = rng.Get();
      value_t *val;

      val = (value_t *)t->Get(&key);
      assert(val && *val == derive_val(key));
    }
  }
}

BENCHMARK_REGISTER_F(BessFixture, BessInlinedGet)->RangeMultiplier(4)->Range(4, 4<<20);

BENCHMARK_MAIN();
