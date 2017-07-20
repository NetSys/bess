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

#include "copy.h"

#include <cstdlib>

#include <benchmark/benchmark.h>
#include <glog/logging.h>
#include <rte_memcpy.h>

#include "random.h"

using bess::utils::CopyInlined;

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

    // for deterministic benchmark results, allocate page-aligned memory.
    // (otherwise CPU cache bank conflict will incur seemingly random results)
    mem_ = static_cast<char *>(aligned_alloc(4096, kMaxSize * 2 + 256));

    dst_ = mem_ + 64 + dst_misalign;
    CHECK_EQ(reinterpret_cast<uintptr_t>(dst_) % 64, dst_misalign);

    // Note the page offset of dst_ and src_ differs by 64.
    // This is also to mitigate the CPU cache effect.
    src_ = mem_ + 64 + kMaxSize + 64 + src_misalign;
    CHECK_EQ(reinterpret_cast<uintptr_t>(src_) % 64, src_misalign);

    Random rng;
    for (size_t i = 0; i < size_; i++) {
      src_[i] = rng.GetRange(254);
    }

    src_[-1] = '\xfe';
    src_[size_] = '\xfe';

    dst_[-1] = '\xff';
    dst_[size_] = '\xff';
  }

  void TearDown(benchmark::State &) override {
    CHECK_EQ(dst_[-1], '\xff');
    //CHECK_EQ(dst_[size_], '\xff');  // Copy(sloppy=true) may violate this

    for (size_t i = 0; i < size_; i++) {
      CHECK_EQ(dst_[i], src_[i]) << "Byte " << i << " is different";
    }

    std::free(mem_);
  }

  char *dst_;
  char *src_;
  size_t size_;

 private:
  static const size_t kMaxSize = 8192;
  char *mem_;
};

BENCHMARK_DEFINE_F(CopyFixture, Copy)(benchmark::State &state) {
  while (state.KeepRunning()) {
    CopyInlined(dst_, src_, size_);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(size_ * state.iterations());
}

BENCHMARK_DEFINE_F(CopyFixture, CopySloppy)(benchmark::State &state) {
  while (state.KeepRunning()) {
    CopyInlined(dst_, src_, size_, true);
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
  // skip argument names for brevity
  //b->ArgNames({"dst_align", "src_align", "size"});
  b->Args({0, 0, 4})
      ->Args({0, 0, 7})
      ->Args({0, 0, 8})
      ->Args({0, 0, 14})
      ->Args({46, 0, 14})
      ->Args({50, 0, 14})
      ->Args({0, 0, 18})
      ->Args({46, 0, 18})
      ->Args({50, 0, 18})
      ->Args({0, 10, 31})
      ->Args({0, 0, 32})
      ->Args({0, 0, 48})
      ->Args({15, 19, 48})
      ->Args({2, 0, 60})
      ->Args({0, 0, 64})
      ->Args({0, 14, 64})
      ->Args({0, 18, 64})
      ->Args({0, 0, 100})
      ->Args({0, 0, 128})
      ->Args({0, 0, 256})
      ->Args({10, 47, 257})
      ->Args({0, 0, 384})
      ->Args({1, 0, 384})
      ->Args({0, 16, 512})
      ->Args({0, 0, 1024})
      ->Args({0, 14, 1500})
      ->Args({0, 18, 1500})
      ->Args({0, 0, 1514})
      ->Args({0, 0, 1518})
      ->Args({19, 4, 2047})
      ->Args({0, 0, 4096});
}

BENCHMARK_REGISTER_F(CopyFixture, Copy)->Apply(SetArguments);
BENCHMARK_REGISTER_F(CopyFixture, CopySloppy)->Apply(SetArguments);
BENCHMARK_REGISTER_F(CopyFixture, RteMemcpy)->Apply(SetArguments);
BENCHMARK_REGISTER_F(CopyFixture, Memcpy)->Apply(SetArguments);

BENCHMARK_MAIN()
