#include <glog/logging.h>
#include <gtest/gtest.h>

int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  // By default, suppress annoying warnings on every death test.
  testing::GTEST_FLAG(death_test_style) = "threadsafe";

  return RUN_ALL_TESTS();
}
