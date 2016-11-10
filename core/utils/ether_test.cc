#include "ether.h"

#include <gtest/gtest.h>

namespace {

using MacAddr = bess::utils::EthHeader::Address;

TEST(EthHeaderTest, AddressInStr) {
  MacAddr a;

  EXPECT_FALSE(a.FromString("0g:12:34:56:78:90"));
  EXPECT_FALSE(a.FromString("0f:12:34:56:78:"));
  EXPECT_FALSE(a.FromString("00:12:34:56:7::90"));

  EXPECT_TRUE(a.FromString("a:b:c:d:e:f"));
  EXPECT_STREQ("0a:0b:0c:0d:0e:0f", a.ToString().c_str());

  EXPECT_TRUE(a.FromString("00:00:00:00:00:00"));
  EXPECT_STREQ("00:00:00:00:00:00", a.ToString().c_str());

  EXPECT_TRUE(a.FromString("12:f:34:56:78:90"));
  EXPECT_STREQ("12:0f:34:56:78:90", a.ToString().c_str());

  MacAddr b("12:0F:34:56:78:90");
  EXPECT_STREQ(b.ToString().c_str(), a.ToString().c_str());
}

TEST(EthHeaderTest, AddrEquality) {
  MacAddr a("a0:17:03:20:b8:9");
  MacAddr b("A0:17:3:20:B8:09");
  MacAddr c("a0:18:3:20:b8:09");

  EXPECT_EQ(a, a);
  EXPECT_EQ(b, b);
  EXPECT_EQ(c, c);

  EXPECT_EQ(a, b);
  EXPECT_EQ(b, a);
  EXPECT_NE(a, c);
  EXPECT_NE(c, a);
  EXPECT_NE(b, c);
  EXPECT_NE(c, b);
}

TEST(EthHeaderTest, RandomAddr) {
  MacAddr a;
  MacAddr b;
  MacAddr c("a0:18:3:20:b8:09");

  a.Randomize();
  b.Randomize();
  c.Randomize();

  EXPECT_NE(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(b, c);

  MacAddr d("a0:18:3:20:b8:09");
  EXPECT_NE(c, d);
}

}  // namespace (unnamed)
