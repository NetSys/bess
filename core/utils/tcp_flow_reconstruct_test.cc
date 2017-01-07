#include "tcp_flow_reconstruct.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include <gtest/gtest.h>
#include <pcap/pcap.h>

namespace bess {
namespace utils {
namespace {

// A parameterized test fixture for testing TCP flows that specifies a PCAP file
// from which to read packets.
class TcpFlowReconstructTest : public ::testing::TestWithParam<const char*> {
 protected:
  virtual void SetUp() {
    // TODO(barath): Due to a problem with some versions of gtest when using the
    //               TEST_P macro, for the moment we just use a single tracefile
    //               for now.
    std::string tracefile_prefix("testdata/test-pktcaptures/tcpflow-http-3");

    char errbuf[2048];
    pcap_t *handle = pcap_open_offline((tracefile_prefix + std::string(".pcap")).c_str(), errbuf);
    ASSERT_TRUE(handle != nullptr);

    const u_char *pcap_pkt;
    struct pcap_pkthdr pcap_hdr;
    while ((pcap_pkt = pcap_next(handle, &pcap_hdr)) != nullptr) {
      ASSERT_EQ(pcap_hdr.caplen, pcap_hdr.len) << "Didn't capture the full packet.";
      Packet *p = new Packet();
      p->set_buffer(p->data());
      memcpy(p->data(), pcap_pkt, pcap_hdr.caplen);
      pkts_.push_back(p);
    }

    // Read in file with good reconstruction.
    std::ifstream f(tracefile_prefix + std::string(".bytes"), std::ios::binary | std::ios::ate);
    ASSERT_FALSE(f.bad());
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);

    data_len_ = size;
    data_ = (char *) malloc(data_len_);
    f.read(data_, data_len_);
  }

  virtual void TearDown() {
    for (Packet *p : pkts_) {
      delete p;
    }

    free(data_);
  }

  // The packets of the pcap trace file.
  std::vector<Packet *> pkts_;

  // The correctly reconstructed raw TCP byte stream.
  char *data_;
  size_t data_len_;
};

// Tests that the constructor initializes the underlying buffers to the
// specified initial size.
TEST(TcpFlowReconstruct, Constructor) {
  TcpFlowReconstruct t1(0), t2(7);
  EXPECT_EQ(1, t1.received_map().size());
  EXPECT_EQ(7, t2.received_map().size());
}

// Tests that reconstructed flows contain the right data when done in order.
TEST_F(TcpFlowReconstructTest, StandardReconstruction) {
  TcpFlowReconstruct t(1);

  for (Packet *p : pkts_) {
    t.InsertPacket(p);
  }

  ASSERT_EQ(data_len_, t.contiguous_len());
  EXPECT_EQ(0, memcmp(t.buf(), data_, data_len_));
}

// Tests that reordering packets doesn't affect the reconstruction.
TEST_F(TcpFlowReconstructTest, ReorderedReconstruction) {
  Packet *syn = pkts_[0]; 

  std::vector<Packet *> pkt_rotation;
  for (size_t i = 1; i < pkts_.size(); ++i) {
    pkt_rotation.push_back(pkts_[i]);
  }
  std::sort(pkt_rotation.begin(), pkt_rotation.end());

  do {
    TcpFlowReconstruct t(1);
    t.InsertPacket(syn);

    for (Packet *p : pkt_rotation) {
      t.InsertPacket(p);
    }

    ASSERT_EQ(data_len_, t.contiguous_len());
    EXPECT_EQ(0, memcmp(t.buf(), data_, data_len_));
  } while (std::next_permutation(pkt_rotation.begin(), pkt_rotation.end()));

}

}  // namespace
}  // namespace utils
}  // namespace bess
