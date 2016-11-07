#include "time.h"

#include <gtest/gtest.h>

TEST(RdtscTest, NonDecreasing) {
  uint64_t a = rdtsc();
  uint64_t b = rdtsc();
  ASSERT_LE(a, b) << "Time stamp counter should not decrease.";
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
