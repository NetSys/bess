#include "ip.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>

using bess::utils::be32_t;

namespace {

using bess::utils::CIDRNetwork;

// Check if CIDRNetwork can be correctly constructed from strings
TEST(IPTest, CIDRInStr) {
  CIDRNetwork cidr_1("192.168.0.1/24");
  EXPECT_EQ((192 << 24) + (168 << 16) + 1, cidr_1.addr.value());
  EXPECT_EQ(0xffffff00, cidr_1.mask.value());

  CIDRNetwork cidr_2("0.0.0.0/0");
  EXPECT_EQ(0, cidr_2.addr.value());
  EXPECT_EQ(0, cidr_2.mask.value());

  CIDRNetwork cidr_3("128.0.0.0/1");
  EXPECT_EQ(128 << 24, cidr_3.addr.value());
  EXPECT_EQ(0x80000000, cidr_3.mask.value());
}

// Check if CIDRNetwork::Match() behaves correctly
TEST(IPTest, CIDRMatch) {
  CIDRNetwork cidr_1("192.168.0.1/24");
  EXPECT_TRUE(cidr_1.Match(be32_t((192 << 24) + (168 << 16) + 254)));
  EXPECT_FALSE(cidr_1.Match(be32_t((192 << 24) + (168 << 16) + (2 << 8) + 1)));

  CIDRNetwork cidr_2("0.0.0.0/0");
  EXPECT_TRUE(cidr_2.Match(be32_t((192 << 24) + (168 << 16) + 254)));
  EXPECT_TRUE(cidr_2.Match(be32_t((192 << 24) + (168 << 16) + (2 << 8) + 1)));

  CIDRNetwork cidr_3("192.168.0.1/32");
  EXPECT_FALSE(cidr_3.Match(be32_t((192 << 24) + (168 << 16) + 254)));
  EXPECT_TRUE(cidr_3.Match(be32_t((192 << 24) + (168 << 16) + 1)));
}

}  // namespace (unnamed)
