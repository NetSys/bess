#include "simd.h"

#include <cstdlib>

#include <benchmark/benchmark.h>
#include <glog/logging.h>
#include <rte_memcpy.h>

class CopyFixture : public benchmark::Fixture {
 protected:
  void SetUp(benchmark::State &state) override {
    // how many bytes are misaligned from 64B boundary?
    size_t dst_misalign = state.range(0);
    CHECK_LT(dst_misalign, 64);

    size_t src_misalign = state.range(1);
    CHECK_LT(src_misalign, 64);

    size_ = state.range(2);
    CHECK_LE(size_, kMaxSize);

    dst_aligned_ = static_cast<char *>(aligned_alloc(64, size_ + 63));
    dst_ = dst_aligned_ + dst_misalign;
    CHECK(reinterpret_cast<uintptr_t>(dst_) % 64 == dst_misalign);

    src_aligned_ = static_cast<char *>(aligned_alloc(64, size_ + 63));
    src_ = src_aligned_ + src_misalign;
    CHECK(reinterpret_cast<uintptr_t>(src_) % 64 == dst_misalign);
  }

  void TearDown(benchmark::State &) override {
    std::free(dst_aligned_);
    std::free(src_aligned_);
  }

  char *dst_;
  char *src_;
  size_t size_;

 private:
  static const size_t kMaxSize = 8192;
  char *dst_aligned_;
  char *src_aligned_;
};

BENCHMARK_DEFINE_F(CopyFixture, CopySloppy)(benchmark::State &state) {
  while (state.KeepRunning()) {
    CopySloppy(dst_, src_, size_);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(size_ * state.iterations());
}

BENCHMARK_DEFINE_F(CopyFixture, RteMemcpy)(benchmark::State &state) {
  while (state.KeepRunning()) {
    rte_memcpy(dst_, src_, size_);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(size_ * state.iterations());
}

BENCHMARK_DEFINE_F(CopyFixture, Memcpy)(benchmark::State &state) {
  while (state.KeepRunning()) {
    memcpy(dst_, src_, size_);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(size_ * state.iterations());
}

static void SetArguments(benchmark::internal::Benchmark *b) {
  b->Args({0, 0, 4})
      ->Args({0, 0, 7})
      ->Args({0, 0, 8})
      ->Args({0, 0, 31})
      ->Args({0, 0, 63})
      ->Args({0, 0, 64})
      ->Args({0, 0, 100})
      ->Args({0, 0, 256})
      ->Args({0, 0, 257})
      ->Args({0, 0, 1024})
      ->Args({0, 0, 4096});
}

BENCHMARK_REGISTER_F(CopyFixture, CopySloppy)->Apply(SetArguments);
BENCHMARK_REGISTER_F(CopyFixture, RteMemcpy)->Apply(SetArguments);
BENCHMARK_REGISTER_F(CopyFixture, Memcpy)->Apply(SetArguments);

BENCHMARK_MAIN()
