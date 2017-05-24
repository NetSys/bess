#include "ip.h"

#include <gtest/gtest.h>

using bess::utils::be32_t;

namespace {

using bess::utils::Ipv4Prefix;

TEST(IPTest, AddressInStr) {
  be32_t a(192 << 24 | 168 << 16 | 100 << 8 | 199);

  std::string str = ToIpv4Address(a);
  EXPECT_EQ(str, "192.168.100.199");

  be32_t b;
  bool ret = ParseIpv4Address(str, &b);
  EXPECT_TRUE(ret);
  EXPECT_EQ(a, b);

  EXPECT_FALSE(ParseIpv4Address("hello", &b));
  EXPECT_FALSE(ParseIpv4Address("1.1.1", &b));
  EXPECT_FALSE(ParseIpv4Address("1.1.256.1", &b));
}

// Check if Ipv4Prefix can be correctly constructed from strings
TEST(IPTest, PrefixInStr) {
  Ipv4Prefix prefix_1("192.168.0.1/24");
  EXPECT_EQ((192 << 24) + (168 << 16) + 1, prefix_1.addr.value());
  EXPECT_EQ(0xffffff00, prefix_1.mask.value());

  Ipv4Prefix prefix_2("0.0.0.0/0");
  EXPECT_EQ(0, prefix_2.addr.value());
  EXPECT_EQ(0, prefix_2.mask.value());

  Ipv4Prefix prefix_3("128.0.0.0/1");
  EXPECT_EQ(128 << 24, prefix_3.addr.value());
  EXPECT_EQ(0x80000000, prefix_3.mask.value());
}

// Check if Ipv4Prefix::Match() behaves correctly
TEST(IPTest, PrefixMatch) {
  Ipv4Prefix prefix_1("192.168.0.1/24");
  EXPECT_TRUE(prefix_1.Match(be32_t((192 << 24) + (168 << 16) + 254)));
  EXPECT_FALSE(
      prefix_1.Match(be32_t((192 << 24) + (168 << 16) + (2 << 8) + 1)));

  Ipv4Prefix prefix_2("0.0.0.0/0");
  EXPECT_TRUE(prefix_2.Match(be32_t((192 << 24) + (168 << 16) + 254)));
  EXPECT_TRUE(prefix_2.Match(be32_t((192 << 24) + (168 << 16) + (2 << 8) + 1)));

  Ipv4Prefix prefix_3("192.168.0.1/32");
  EXPECT_FALSE(prefix_3.Match(be32_t((192 << 24) + (168 << 16) + 254)));
  EXPECT_TRUE(prefix_3.Match(be32_t((192 << 24) + (168 << 16) + 1)));
}

}  // namespace (unnamed)
