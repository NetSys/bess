#include "ip.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>

namespace {

using bess::utils::CIDRNetwork;

// Check if CIDRNetwork can be correctly constructed from strings
TEST(IPTest, CIDRInStr) {
  CIDRNetwork cidr_1("192.168.0.1/24");

  EXPECT_EQ(htonl((192 << 24) + (168 << 16) + 1), cidr_1.addr);
  EXPECT_EQ(htonl(0xffffff00), cidr_1.mask);

  CIDRNetwork cidr_2("0.0.0.0/0");
  EXPECT_EQ(htonl(0), cidr_2.addr);
  EXPECT_EQ(htonl(0x0), cidr_2.mask);
}

// Check if CIDRNetwork::Match() behaves correctly
TEST(IPTest, CIDRMatch) {
  CIDRNetwork cidr_1("192.168.0.1/24");
  EXPECT_TRUE(cidr_1.Match(htonl((192 << 24) + (168 << 16) + 254)));
  EXPECT_FALSE(cidr_1.Match(htonl((192 << 24) + (168 << 16) + (2 << 8) + 1)));

  CIDRNetwork cidr_2("192.168.0.1/0");
  EXPECT_TRUE(cidr_2.Match(htonl((192 << 24) + (168 << 16) + 254)));
  EXPECT_TRUE(cidr_2.Match(htonl((192 << 24) + (168 << 16) + (2 << 8) + 1)));

  CIDRNetwork cidr_3("192.168.0.1/32");
  EXPECT_FALSE(cidr_3.Match(htonl((192 << 24) + (168 << 16) + 254)));
  EXPECT_TRUE(cidr_3.Match(htonl((192 << 24) + (168 << 16) + 1)));
}

}  // namespace (unnamed)
