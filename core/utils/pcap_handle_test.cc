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
