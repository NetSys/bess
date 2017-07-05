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

#include "time.h"

#include <gtest/gtest.h>

TEST(RdtscTest, NonDecreasing) {
  uint64_t a = rdtsc();
  uint64_t b = rdtsc();
  ASSERT_LE(a, b) << "Time stamp counter should not decrease.";
}

TEST(TscToUs, Frequency) {
  ASSERT_NE(0, tsc_hz) << "tsc_hz has not been initialized";
  EXPECT_LE(500000000, tsc_hz) << "tsc_hz < 500MHz?";
  EXPECT_GE(10000000000, tsc_hz) << "tsc_hz > 10GHz?";
}

TEST(TscToUs, NonNegative) {
  ASSERT_LE(0, tsc_to_us(0))
      << "Conversion should never result in negative time.";
}

TEST(GetEpochTime, NonNegative) {
  ASSERT_LE(0, get_epoch_time()) << "Time should never be negative.";
}

TEST(GetCpuTime, NonNegative) {
  ASSERT_LE(0, get_cpu_time()) << "Time should never be negative.";
}
