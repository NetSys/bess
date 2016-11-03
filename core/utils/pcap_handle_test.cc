// Some basic unit tests for PcapHandle class -- complete testing requires
// integration testing to make sure it can actually talk to driver.

// TODO: Integration tests later for Recv, Send, Reset, and dev constructor.

#include "pcap_handle.h"
#include <gtest/gtest.h>
#include <string>

class PcapHandleFixtureTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    // Can't actually set up port w/o integration test so fake internal state
    pcap_with_fake_handle.handle_ = (pcap_t*)342;
  }

  virtual void TearDown() {
    // So destructor doesn't try to close fake connection.
    pcap_with_fake_handle.handle_ = nullptr;
  }

  PcapHandle pcap_with_fake_handle;
};

// With no parameters, should always be uninitialized.
TEST(PcapHandleBasicTest, EmptyConstructor) {
  PcapHandle p;
  ASSERT_FALSE(p.is_initialized());
  ASSERT_NE(0, p.SendPacket(nullptr, 44));
  int caplen = 72;
  ASSERT_EQ(nullptr, p.RecvPacket(&caplen));
  ASSERT_EQ(0, caplen);
}

// Should result in an uninitialized device if binding to device fails.
TEST(PcapHandleBasicTest, BadDevice) {
  PcapHandle p("sangjinhan");
  ASSERT_FALSE(p.is_initialized());
  ASSERT_NE(0, p.SendPacket(nullptr, 44));
  int caplen = 72;
  ASSERT_EQ(nullptr, p.RecvPacket(&caplen));
  ASSERT_EQ(0, caplen);
}

// Check that it knows it's initialized if it has a pcap handle.
TEST_F(PcapHandleFixtureTest, IsInitialized) {
  ASSERT_TRUE(pcap_with_fake_handle.is_initialized());
}

// Move operator.
TEST_F(PcapHandleFixtureTest, MoveOperator) {
  ASSERT_TRUE(pcap_with_fake_handle.is_initialized());
  PcapHandle MoveTo = std::move(pcap_with_fake_handle);
  ASSERT_FALSE(pcap_with_fake_handle.is_initialized());
  ASSERT_TRUE(MoveTo.is_initialized());
  pcap_with_fake_handle = std::move(MoveTo);
  ASSERT_FALSE(MoveTo.is_initialized());
}

// Test the move constructor.
TEST_F(PcapHandleFixtureTest, MoveConstructor) {
  PcapHandle MoveTo(std::move(pcap_with_fake_handle));
  ASSERT_FALSE(pcap_with_fake_handle.is_initialized());
  ASSERT_TRUE(MoveTo.is_initialized());
  pcap_with_fake_handle = std::move(MoveTo);
  ASSERT_FALSE(MoveTo.is_initialized());
}
