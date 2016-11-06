// Some basic unit tests for PcapHandle class -- complete testing requires
// integration testing to make sure it can actually talk to driver.

// TODO: Integration tests later for Recv, Send, Reset, and dev constructor.

#include "pcap_handle.h"

#include <gtest/gtest.h>

#include "pcap.h"

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
  PcapHandle p("");
  ASSERT_FALSE(p.is_initialized());
  ASSERT_NE(0, p.SendPacket(nullptr, 44));
  int caplen = 72;
  ASSERT_EQ(nullptr, p.RecvPacket(&caplen));
  ASSERT_EQ(0, caplen);
}

// Move assignment.
TEST(PcapHandleFixtureTest, MoveAssignment) {
  pcap_t *dummy_handle = pcap_open_dead(72, DLT_EN10MB);
  PcapHandle pcap_with_fake_handle(dummy_handle);
  ASSERT_TRUE(pcap_with_fake_handle.is_initialized());

  PcapHandle MoveTo;
  ASSERT_FALSE(MoveTo.is_initialized());

  MoveTo = std::move(pcap_with_fake_handle);
  ASSERT_FALSE(pcap_with_fake_handle.is_initialized());
  ASSERT_TRUE(MoveTo.is_initialized());
}

// Move constructor.
TEST(PcapHandleFixtureTest, MoveConstructor) {
  pcap_t *dummy_handle = pcap_open_dead(72, DLT_EN10MB);
  PcapHandle pcap_with_fake_handle(dummy_handle);
  ASSERT_TRUE(pcap_with_fake_handle.is_initialized());

  PcapHandle MoveTo(std::move(pcap_with_fake_handle));
  ASSERT_FALSE(pcap_with_fake_handle.is_initialized());
  ASSERT_TRUE(MoveTo.is_initialized());
}
