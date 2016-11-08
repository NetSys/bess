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
