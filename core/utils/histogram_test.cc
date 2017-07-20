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

#include "histogram.h"

#include <gtest/gtest.h>

namespace {

TEST(HistogramTest, U32Quartiles) {
  Histogram<uint32_t> hist(1000, 1);
  std::vector<uint32_t> values = {1, 2, 3, 4, 5, 1001};
  for (uint32_t x : values) {
    hist.insert(x);
  }
  ASSERT_EQ(1, hist.above_threshold());
  ASSERT_EQ(2, hist.min());
  ASSERT_EQ(6, hist.max());
  ASSERT_EQ(4, hist.avg());
  ASSERT_EQ(5, hist.count());
  ASSERT_EQ(20, hist.total());
  ASSERT_EQ(1, hist.percentile(25));   // 25th percentile
  ASSERT_EQ(2, hist.percentile(50));   // 50th percentile
  ASSERT_EQ(3, hist.percentile(75));   // 75th percentile
  ASSERT_EQ(5, hist.percentile(100));  // 100th percentile
}

TEST(HistogramTest, DoubleQuartiles) {
  Histogram<double> hist(1000, 0.5);
  std::vector<double> values = {1.0, 1.0, 2.0, 3.0, 4.0, 5.0};
  for (double x : values) {
    hist.insert(x);
  }
  ASSERT_EQ(0, hist.above_threshold());
  ASSERT_DOUBLE_EQ(1.5, hist.min());
  ASSERT_DOUBLE_EQ(5.5, hist.max());
  ASSERT_DOUBLE_EQ(19.0 / 6.0, hist.avg());
  ASSERT_EQ(6, hist.count());
  ASSERT_DOUBLE_EQ(19.0, hist.total());
  ASSERT_DOUBLE_EQ(1.0, hist.percentile(25));   // 25th percentile
  ASSERT_DOUBLE_EQ(2.0, hist.percentile(50));   // 50th percentile
  ASSERT_DOUBLE_EQ(3.0, hist.percentile(75));   // 75th percentile
  ASSERT_DOUBLE_EQ(5.0, hist.percentile(100));  // 100th percentile
}
}
