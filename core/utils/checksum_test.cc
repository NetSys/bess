#include "checksum.h"

#include <cstdint>

#include <gtest/gtest.h>
#include <rte_config.h>
#include <rte_ip.h>

#include "random.h"

using namespace bess::utils;

namespace {

Random rd;
static int TestLoopCount = 100000;

// Tests generic checksum
TEST(ChecksumTest, GenericChecksum) {
  uint32_t buf[40] = {
      0x45000032, 0x00010000, 0x40060000, 0x0c22384e, 0xac0c3763, 0x45000032,
      0x00010000, 0x40060000, 0x0c22384e, 0xac0c3763, 0x45000032, 0x00010000,
      0x40060000, 0x0c22384e, 0xac0c3763, 0x45000032, 0x00010000, 0x40060000,
      0x0c22384e, 0xac0c3763, 0x45000032, 0x00010000, 0x40060000, 0x0c22384e,
      0xac0c3763, 0x45000032, 0x00010000, 0x40060000, 0x0c22384e, 0xac0c3763,
      0x45000032, 0x00010000, 0x40060000, 0x0c22384e, 0xac0c3763, 0x45000032,
      0x00010000, 0x40060000, 0x0c22384e, 0xac0c3763};

  // choose byte size 159, 160 to enter the all loops in the test functions
  uint16_t cksum_bess = CalculateGenericChecksum(buf, 160);
  uint16_t cksum_dpdk = ~rte_raw_cksum(buf, 160);  // take the complement
  EXPECT_EQ(cksum_dpdk, cksum_bess);

  EXPECT_TRUE(VerifyGenericChecksum(buf, 160, cksum_bess));
  EXPECT_TRUE(VerifyGenericChecksum(buf, 160, cksum_dpdk));

  cksum_bess = CalculateGenericChecksum(buf, 159);
  cksum_dpdk = ~rte_raw_cksum(buf, 159);  // take the complement
  EXPECT_EQ(cksum_dpdk, cksum_bess);

  EXPECT_TRUE(VerifyGenericChecksum(buf, 159, cksum_bess));
  EXPECT_TRUE(VerifyGenericChecksum(buf, 159, cksum_dpdk));

  for (int i = 0; i < TestLoopCount; i++) {
    for (int j = 0; j < 40; j++) {
      buf[j] = rd.Get();
    }

    cksum_bess = CalculateGenericChecksum(buf, 160);
    cksum_dpdk = ~rte_raw_cksum(buf, 160);  // take the complement
    EXPECT_EQ(cksum_dpdk, cksum_bess);
  }
}

// Tests IP checksum
TEST(ChecksumTest, Ipv4NoOptChecksum) {
  char buf[1514] = {0};  // ipv4 header w/o options

  bess::utils::Ipv4Header *ip =
      reinterpret_cast<bess::utils::Ipv4Header *>(buf);

  ip->version = 4;
  ip->header_length = 5;
  ip->type_of_service = 0;
  ip->length = htons(20);
  ip->fragment_offset = 0;
  ip->ttl = 10;
  ip->protocol = 0x06;  // tcp
  ip->src = 0x12345678;
  ip->dst = 0x12347890;

  uint16_t cksum_dpdk = rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip));
  uint16_t cksum_bess = CalculateIpv4NoOptChecksum(*ip);
  EXPECT_EQ(cksum_dpdk, cksum_bess);

  // bess excludes to checksum fields to calculate ip checksum
  ip->checksum = 0x7823;
  cksum_bess = CalculateIpv4NoOptChecksum(*ip);
  EXPECT_EQ(cksum_dpdk, cksum_bess);

  ip->checksum = cksum_bess;
  EXPECT_TRUE(VerifyIpv4NoOptChecksum(*ip));

  ip->checksum = 0x0000;  // for dpdk

  for (int i = 0; i < TestLoopCount; i++) {
    ip->src = rd.Get();
    ip->dst = rd.Get();

    cksum_dpdk = rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip));
    cksum_bess = CalculateIpv4NoOptChecksum(*ip);

    if (cksum_dpdk == 0xffff) {
      // While the value of IP/TCP checksum field must not be -0 (0xffff),
      // but DPDK often (incorrectly) gives that value. (RFC 768, 1071, 1624)
      EXPECT_EQ(0, cksum_bess);
    } else {
      EXPECT_EQ(cksum_dpdk, cksum_bess);
    }
  }
}

// Tests TCP checksum
TEST(ChecksumTest, TcpChecksum) {
  char buf[1514] = {0};  // ipv4 header + tcp header

  bess::utils::Ipv4Header *ip =
      reinterpret_cast<bess::utils::Ipv4Header *>(buf);

  bess::utils::TcpHeader *tcp =
      reinterpret_cast<bess::utils::TcpHeader *>(ip + 1);

  ip->version = 4;
  ip->header_length = 5;
  ip->type_of_service = 0;
  ip->length = htons(40);
  ip->fragment_offset = 0;
  ip->ttl = 10;
  ip->protocol = 0x06;  // tcp
  ip->src = 0x12345678;
  ip->dst = 0x12347890;

  tcp->src_port = 0x0024;
  tcp->dst_port = 0x2097;
  tcp->seq_num = 0x67546354;
  tcp->ack_num = 0x98461732;

  uint16_t cksum_dpdk =
      rte_ipv4_udptcp_cksum(reinterpret_cast<const ipv4_hdr *>(ip), tcp);
  uint16_t cksum_bess = CalculateIpv4TcpChecksum(*ip, *tcp);
  EXPECT_EQ(cksum_dpdk, cksum_bess);

  // bess excludes the checksum field to calculate tcp checksum
  tcp->checksum = 0x0987;
  cksum_bess = CalculateIpv4TcpChecksum(*ip, *tcp);
  EXPECT_EQ(cksum_dpdk, cksum_bess);

  tcp->checksum = cksum_bess;
  EXPECT_TRUE(VerifyIpv4TcpChecksum(*ip, *tcp));

  for (int i = 0; i < TestLoopCount; i++) {
    uint16_t *p = reinterpret_cast<uint16_t *>(buf);

    for (int j = 0; j < 5; j++) {
      p[j] = rd.Get();

      ip->version = 4;
      ip->header_length = 5;
      ip->length = htons(40);
      ip->protocol = 0x06;     // tcp
      ip->checksum = 0x0000;   // for dpdk
      tcp->checksum = 0x0000;  // for dpdk
    }

    cksum_dpdk = rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip));
    cksum_bess = CalculateIpv4NoOptChecksum(*ip);

    if (cksum_dpdk == 0xffff) {
      // While the value of IP/TCP checksum field must not be -0 (0xffff),
      // but DPDK often (incorrectly) gives that value. (RFC 768, 1071, 1624)
      EXPECT_EQ(0, cksum_bess);
    } else {
      EXPECT_EQ(cksum_dpdk, cksum_bess);
    }
  }
}

// Tests incremental checksum update for unsigned 16-bit integer
TEST(ChecksumTest, IncrementalUpdateChecksum16) {
  uint16_t old16 = 0x4500;
  uint16_t new16 = 0x1234;
  uint16_t buf[5] = {old16, 0x0001, 0x4006, 0x0c22, 0xac0c};
  uint16_t cksum_old = CalculateGenericChecksum(buf, 10);

  buf[0] = new16;
  uint16_t cksum_new = CalculateGenericChecksum(buf, 10);
  uint16_t cksum_update =
      CalculateChecksumIncremental16(cksum_old, old16, new16);

  EXPECT_EQ(cksum_new, cksum_update);

  for (int i = 0; i < TestLoopCount; i++) {
    for (int j = 0; j < 5; j++)
      buf[j] = rd.Get() >> 16;

    cksum_old = CalculateGenericChecksum(buf, 10);

    old16 = buf[0];
    new16 = buf[0] = rd.Get() >> 16;
    cksum_new = CalculateGenericChecksum(buf, 10);
    cksum_update = CalculateChecksumIncremental16(cksum_old, old16, new16);
    EXPECT_EQ(cksum_new, cksum_update);
  }
}

// Tests incremental checksum update for unsigned 32-bit integer
TEST(ChecksumTest, IncrementalUpdateChecksum32) {
  uint32_t old32 = 0x45000032;
  uint32_t new32 = 0x12341234;
  uint32_t buf[5] = {0x45000032, 0x00010000, 0x40060000, 0x0c22384e,
                     0xac0c3763};

  uint16_t cksum_old = CalculateGenericChecksum(buf, 20);

  buf[0] = new32;
  uint16_t cksum_new = CalculateGenericChecksum(buf, 20);
  uint16_t cksum_update =
      CalculateChecksumIncremental32(cksum_old, old32, new32);

  EXPECT_EQ(cksum_new, cksum_update);

  for (int i = 0; i < TestLoopCount; i++) {
    for (int j = 0; j < 5; j++) {
      buf[j] = rd.Get();
    }

    cksum_old = CalculateGenericChecksum(buf, 20);

    old32 = buf[0];
    new32 = buf[0] = rd.Get();
    cksum_new = CalculateGenericChecksum(buf, 20);
    cksum_update = CalculateChecksumIncremental32(cksum_old, old32, new32);
    EXPECT_EQ(cksum_new, cksum_update);
  }
}

// Tests incremental checksum update with source IP/port update
TEST(ChecksumTest, IncrementalUpdateSrcIpPort) {
  char buf[1514] = {0};

  bess::utils::Ipv4Header *ip =
      reinterpret_cast<bess::utils::Ipv4Header *>(buf);

  bess::utils::TcpHeader *tcp =
      reinterpret_cast<bess::utils::TcpHeader *>(ip + 1);

  ip->version = 4;
  ip->header_length = 5;
  ip->type_of_service = 0;
  ip->length = htons(40);
  ip->fragment_offset = 0;
  ip->ttl = 10;
  ip->protocol = 0x06;  // tcp
  ip->src = 0x12345678;
  ip->dst = 0x12347890;

  tcp->src_port = 0x0024;
  tcp->dst_port = 0x2097;
  tcp->seq_num = 0x67546354;
  tcp->ack_num = 0x98461732;

  ip->checksum = CalculateIpv4NoOptChecksum(*ip);
  tcp->checksum = CalculateIpv4TcpChecksum(*ip, *tcp);
  EXPECT_TRUE(VerifyIpv4NoOptChecksum(*ip));
  EXPECT_TRUE(VerifyIpv4TcpChecksum(*ip, *tcp));

  for (int i = 0; i < TestLoopCount; i++) {
    uint32_t src_ip_old = ip->src;
    uint16_t src_port_old = tcp->src_port;
    uint16_t ip_cksum_old = ip->checksum;
    uint16_t tcp_cksum_old = tcp->checksum;

    for (int j = 0; j < 5; j++) {
      ip->src = rd.Get();
      tcp->src_port = rd.Get() >> 16;
    }

    ip->checksum =
        CalculateChecksumIncremental32(ip_cksum_old, src_ip_old, ip->src);
    EXPECT_TRUE(VerifyIpv4NoOptChecksum(*ip));

    tcp->checksum =
        CalculateChecksumIncremental32(tcp_cksum_old, src_ip_old, ip->src);
    tcp->checksum = CalculateChecksumIncremental16(tcp->checksum, src_port_old,
                                                   tcp->src_port);
    EXPECT_TRUE(VerifyIpv4TcpChecksum(*ip, *tcp));
  }
}
}  // namespace (unnamed)
