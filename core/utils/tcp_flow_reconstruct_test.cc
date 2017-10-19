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

#include "tcp_flow_reconstruct.h"

#include <algorithm>
#include <fstream>

#include <gtest/gtest.h>
#include <pcap/pcap.h>

namespace bess {
namespace utils {
namespace {

// A parameterized test fixture for testing TCP flows that specifies a PCAP file
// from which to read packets.
class TcpFlowReconstructTest : public ::testing::TestWithParam<const char *> {
 protected:
  virtual void SetUp() {
    // TODO(barath): Due to a problem with some versions of gtest when using the
    //               TEST_P macro, for the moment we just use a single tracefile
    //               for now.
    std::string tracefile_prefix = "testdata/test-pktcaptures/tcpflow-http-3";

    char errbuf[PCAP_ERRBUF_SIZE];
    handle_ =
        pcap_open_offline((tracefile_prefix + ".pcap").c_str(), errbuf);
    ASSERT_TRUE(handle_ != nullptr);

    const u_char *pcap_pkt;
    struct pcap_pkthdr pcap_hdr;
    while ((pcap_pkt = pcap_next(handle_, &pcap_hdr)) != nullptr) {
      ASSERT_EQ(pcap_hdr.caplen, pcap_hdr.len)
          << "Didn't capture the full packet.";
      Packet *p = new Packet();
      p->set_buffer(p->data());
      bess::utils::Copy(p->data(), pcap_pkt, pcap_hdr.caplen);
      p->set_data_len(pcap_hdr.caplen);
      p->set_total_len(pcap_hdr.caplen);
      pkts_.push_back(p);
    }

    // Read in file with good reconstruction.
    std::ifstream f(tracefile_prefix + ".bytes", std::ios::binary);
    ASSERT_FALSE(f.bad());

    f >> std::noskipws;
    std::istream_iterator<char> start(f), end;
    std::copy(start, end, std::back_inserter(bytestream_));
  }

  virtual void TearDown() {
    for (Packet *p : pkts_) {
      delete p;
    }
    if (handle_) {
      pcap_close(handle_);
    }
  }

  // The packets of the pcap trace file.
  std::vector<Packet *> pkts_;

  // The correctly reconstructed raw TCP byte stream.
  std::vector<char> bytestream_;

  pcap_t *handle_;
};

// Tests that the constructor initializes the underlying buffers to the
// specified initial size.
TEST(TcpFlowReconstruct, Constructor) {
  TcpFlowReconstruct t1, t2(7);
  EXPECT_EQ(1024, t1.buf_size());
  EXPECT_EQ(7, t2.buf_size());
}

// Tests that reconstructed flows contain the right data when done in order.
TEST_F(TcpFlowReconstructTest, StandardReconstruction) {
  TcpFlowReconstruct t(1);

  for (Packet *p : pkts_) {
    ASSERT_TRUE(t.InsertPacket(p));
  }

  ASSERT_EQ(bytestream_.size(), t.contiguous_len());
  EXPECT_EQ(0, memcmp(t.buf(), bytestream_.data(), bytestream_.size()));
}

// Tests that reordering packets doesn't affect the reconstruction.
TEST_F(TcpFlowReconstructTest, ReorderedReconstruction) {
  Packet *syn = pkts_[0];

  std::vector<Packet *> pkt_rotation;
  for (size_t i = 1; i < pkts_.size(); ++i) {
    int ack_size = sizeof(Ethernet) + sizeof(Ipv4) + sizeof(Tcp);
    // Skip pure ACK packets for the permutations
    if (pkts_[i]->head_len() > ack_size) {
      pkt_rotation.push_back(pkts_[i]);
    }
  }
  std::sort(pkt_rotation.begin(), pkt_rotation.end());

  do {
    TcpFlowReconstruct t;
    ASSERT_TRUE(t.InsertPacket(syn));

    for (Packet *p : pkt_rotation) {
      ASSERT_TRUE(t.InsertPacket(p));
    }

    ASSERT_EQ(bytestream_.size(), t.contiguous_len());
    EXPECT_EQ(0, memcmp(t.buf(), bytestream_.data(), bytestream_.size()));
  } while (std::next_permutation(pkt_rotation.begin(), pkt_rotation.end()));
}

// Tests that we reject packet insertion without the SYN.
TEST_F(TcpFlowReconstructTest, MissingSyn) {
  Packet *syn = pkts_[0];
  Packet *nonsyn = pkts_[1];

  TcpFlowReconstruct t(1);
  ASSERT_FALSE(t.InsertPacket(nonsyn));
  ASSERT_TRUE(t.InsertPacket(syn));
  ASSERT_TRUE(t.InsertPacket(nonsyn));
}

}  // namespace
}  // namespace utils
}  // namespace bess
