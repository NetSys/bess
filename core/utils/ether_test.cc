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

#include "ether.h"

#include <gtest/gtest.h>

namespace {

using MacAddr = bess::utils::Ethernet::Address;

TEST(EthernetTest, AddressInStr) {
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

TEST(EthernetTest, AddrEquality) {
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

TEST(EthernetTest, RandomAddr) {
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
