/*
 * Tests for the RoundRobin module.
 */
#include <gtest/gtest.h>
#include "round_robin.h"

class RoundRobinTest : public ::testing::Test {
  protected:
    virtual void SetUp() {}

    RoundRobin rr;
    // virtual void TearDown() {}
};

TEST_F(RoundRobinTest, PositiveGates){
  EXPECT_GT(1, 0) << "Can't have a negative number of gates.";
}
