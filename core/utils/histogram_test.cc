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
  ASSERT_EQ(1.5, hist.min());
  ASSERT_EQ(5.5, hist.max());
  ASSERT_EQ(19.0 / 6.0, hist.avg());
  ASSERT_EQ(6, hist.count());
  ASSERT_EQ(19.0, hist.total());
  ASSERT_EQ(1.0, hist.percentile(25));   // 25th percentile
  ASSERT_EQ(2.0, hist.percentile(50));   // 50th percentile
  ASSERT_EQ(3.0, hist.percentile(75));   // 75th percentile
  ASSERT_EQ(5.0, hist.percentile(100));  // 100th percentile
}
}
