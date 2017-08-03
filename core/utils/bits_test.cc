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

#include "bits.h"

#include <gtest/gtest.h>

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintBytes(const std::string &label, const std::vector<uint8_t> &buf) {
  std::cout << label << ": ";
  for (uint8_t b : buf) {
    std::cout << std::hex << std::setw(2) << std::setfill('0') << int(b) << " ";
  }
  std::cout << std::endl;
}

void SetupBuffers(std::vector<uint8_t> *a, std::vector<uint8_t> *b,
                  size_t len) {
  a->clear();
  b->clear();
  for (size_t i = 0; i < len; i++) {
    a->push_back(i + 1);
    b->push_back(i + 1);
  }
}

// Shifting ------------------------------------------------------------------

TEST(ShiftRight, ShortBuffer) {
  const size_t kLength = 5;
  std::vector<std::vector<uint8_t>> exp = {
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE}, {0x00, 0xAA, 0xBB, 0xCC, 0xDD},
      {0x00, 0x00, 0xAA, 0xBB, 0xCC}, {0x00, 0x00, 0x00, 0xAA, 0xBB},
      {0x00, 0x00, 0x00, 0x00, 0xAA}, {0x00, 0x00, 0x00, 0x00, 0x00}};
  for (size_t i = 0; i < exp.size(); i++) {
    std::vector<uint8_t> buf = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    bess::utils::ShiftBytesRightSmall(buf.data(), kLength, i);
    int ret = memcmp(exp[i].data(), buf.data(), kLength);
    if (ret != 0) {
      EXPECT_TRUE(false) << "equality check failed for shift: " << i;
      PrintBytes("buf", buf);
      PrintBytes("exp", exp[i]);
    }
  }
}

TEST(ShiftRight, Aligned) {
  std::vector<size_t> lengths = {8, 16, 24, 32};
  std::vector<size_t> shifts = {1, 2, 3, 5, 7, 13};
  std::vector<uint8_t> buf, exp;

  for (size_t len : lengths) {
    for (size_t shift : shifts) {
      SetupBuffers(&buf, &exp, len);
      bess::utils::ShiftBytesRightSmall(exp.data(), len, shift);
      bess::utils::ShiftBytesRight(buf.data(), len, shift);
      int ret = memcmp(exp.data(), buf.data(), len);
      if (ret != 0) {
        EXPECT_TRUE(false) << "equality check failed for len: " << len
                           << ", shift: " << shift;
        PrintBytes("buf", buf);
        PrintBytes("exp", exp);
      }
    }
  }
}

TEST(ShiftRight, Unaligned) {
  std::vector<size_t> lengths = {9, 10, 11, 12, 13, 14, 15};
  std::vector<size_t> shifts = {1, 2, 3, 5, 7, 13};
  std::vector<uint8_t> buf, exp;

  for (size_t len : lengths) {
    for (size_t shift : shifts) {
      SetupBuffers(&buf, &exp, len);
      bess::utils::ShiftBytesRightSmall(exp.data(), len, shift);
      bess::utils::ShiftBytesRight(buf.data(), len, shift);
      int ret = memcmp(exp.data(), buf.data(), len);
      if (ret != 0) {
        EXPECT_TRUE(false) << "equality check failed for len: " << len
                           << ", shift: " << shift;
        PrintBytes("buf", buf);
        PrintBytes("exp", exp);
      }
    }
  }
}

TEST(ShiftLeft, ShortBuffer) {
  const size_t kLength = 5;
  std::vector<std::vector<uint8_t>> exp = {
      {0xAA, 0xBB, 0xCC, 0xDD, 0xEE}, {0xBB, 0xCC, 0xDD, 0xEE, 0x00},
      {0xCC, 0xDD, 0xEE, 0x00, 0x00}, {0xDD, 0xEE, 0x00, 0x00, 0x00},
      {0xEE, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00}};
  for (size_t i = 0; i < exp.size(); i++) {
    std::vector<uint8_t> buf = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    bess::utils::ShiftBytesLeftSmall(buf.data(), kLength, i);
    int ret = memcmp(exp[i].data(), buf.data(), kLength);
    if (ret != 0) {
      EXPECT_TRUE(false) << "equality check failed for shift: " << i;
      PrintBytes("buf", buf);
      PrintBytes("exp", exp[i]);
    }
  }
}

TEST(ShiftLeft, Aligned) {
  std::vector<size_t> lengths = {8, 16, 24, 32};
  std::vector<size_t> shifts = {1, 2, 3, 5, 7, 13};
  std::vector<uint8_t> buf, exp;

  for (size_t len : lengths) {
    for (size_t shift : shifts) {
      SetupBuffers(&buf, &exp, len);
      bess::utils::ShiftBytesLeftSmall(exp.data(), len, shift);
      bess::utils::ShiftBytesLeft(buf.data(), len, shift);
      int ret = memcmp(exp.data(), buf.data(), len);
      if (ret != 0) {
        EXPECT_TRUE(false) << "equality check failed for len: " << len
                           << ", shift: " << shift;
        PrintBytes("buf", buf);
        PrintBytes("exp", exp);
      }
    }
  }
}

TEST(ShiftLeft, Unaligned) {
  std::vector<size_t> lengths = {9, 10, 11, 12, 13, 14, 15};
  std::vector<size_t> shifts = {1, 2, 3, 5, 7, 13};
  std::vector<uint8_t> buf, exp;

  for (size_t len : lengths) {
    for (size_t shift : shifts) {
      SetupBuffers(&buf, &exp, len);
      bess::utils::ShiftBytesLeftSmall(exp.data(), len, shift);
      bess::utils::ShiftBytesLeft(buf.data(), len, shift);
      int ret = memcmp(exp.data(), buf.data(), len);
      if (ret != 0) {
        EXPECT_TRUE(false) << "equality check failed for len: " << len
                           << ", shift: " << shift;
        PrintBytes("buf", buf);
        PrintBytes("exp", exp);
      }
    }
  }
}

// Masking -------------------------------------------------------------------
TEST(Mask, SmallAllBits) {
  const size_t kLength = 5;
  std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04, 0x05};
  std::vector<uint8_t> mask = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  std::vector<uint8_t> exp = {0x01, 0x02, 0x03, 0x04, 0x05};

  bess::utils::MaskBytesSmall(buf.data(), mask.data(), kLength);
  int ret = memcmp(exp.data(), buf.data(), kLength);
  if (ret != 0) {
    EXPECT_TRUE(false) << "equality check failed";
    PrintBytes("buf", buf);
    PrintBytes("exp", exp);
  }
}

TEST(Mask, SmallNoBits) {
  const size_t kLength = 5;
  std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04, 0x05};
  std::vector<uint8_t> mask = {0x00, 0x00, 0x00, 0x00, 0x00};
  std::vector<uint8_t> exp = {0x00, 0x00, 0x00, 0x00, 0x00};

  bess::utils::MaskBytesSmall(buf.data(), mask.data(), kLength);
  int ret = memcmp(exp.data(), buf.data(), kLength);
  if (ret != 0) {
    EXPECT_TRUE(false) << "equality check failed";
    PrintBytes("buf", buf);
    PrintBytes("exp", exp);
  }
}

TEST(Mask, SmallSomeBits) {
  const size_t kLength = 5;
  std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04, 0x05};
  std::vector<uint8_t> mask = {0x00, 0x00, 0xFF, 0x00, 0x00};
  std::vector<uint8_t> exp = {0x00, 0x00, 0x03, 0x00, 0x00};

  bess::utils::MaskBytesSmall(buf.data(), mask.data(), kLength);
  int ret = memcmp(exp.data(), buf.data(), kLength);
  if (ret != 0) {
    EXPECT_TRUE(false) << "equality check failed";
    PrintBytes("buf", buf);
    PrintBytes("exp", exp);
  }
}

TEST(Mask, LongAligned) {
  const size_t kLength = 8;
  std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  std::vector<uint8_t> mask = {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00};
  std::vector<uint8_t> exp = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00};

  bess::utils::MaskBytes(buf.data(), mask.data(), kLength);
  int ret = memcmp(exp.data(), buf.data(), kLength);
  if (ret != 0) {
    EXPECT_TRUE(false) << "equality check failed";
    PrintBytes("buf", buf);
    PrintBytes("exp", exp);
  }
}

TEST(Mask, LongUnaligned) {
  const size_t kLength = 10;
  std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04, 0x05,
                              0x06, 0x07, 0x08, 0x09, 0x0A};
  std::vector<uint8_t> mask = {0x00, 0x00, 0x00, 0x00, 0x00,
                               0xFF, 0x00, 0x00, 0x00, 0x00};
  std::vector<uint8_t> exp = {0x00, 0x00, 0x00, 0x00, 0x00,
                              0x06, 0x00, 0x00, 0x00, 0x00};

  bess::utils::MaskBytes(buf.data(), mask.data(), kLength);
  int ret = memcmp(exp.data(), buf.data(), kLength);
  if (ret != 0) {
    EXPECT_TRUE(false) << "equality check failed";
    PrintBytes("buf", buf);
    PrintBytes("exp", exp);
  }
}

TEST(Mask, ExtraLongAligned) {
  const size_t kLength = 32;
  std::vector<uint8_t> buf = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                              0x09, 0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                              0x07, 0x08, 0x09, 0x0A, 0x01, 0x02, 0x03, 0x04,
                              0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x01, 0x02};
  std::vector<uint8_t> mask = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00};
  std::vector<uint8_t> exp = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00};

  bess::utils::MaskBytes(buf.data(), mask.data(), kLength);
  int ret = memcmp(exp.data(), buf.data(), kLength);
  if (ret != 0) {
    EXPECT_TRUE(false) << "equality check failed";
    PrintBytes("buf", buf);
    PrintBytes("exp", exp);
  }
}

TEST(Mask, ExtraLongUnAligned) {
  const size_t kLength = 33;
  std::vector<uint8_t> buf = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x01,
      0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x01, 0x02,
      0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x01, 0x02, 0xAB};
  std::vector<uint8_t> mask = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0xFF};
  std::vector<uint8_t> exp = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0xAB};

  bess::utils::MaskBytes(buf.data(), mask.data(), kLength);
  int ret = memcmp(exp.data(), buf.data(), kLength);
  if (ret != 0) {
    EXPECT_TRUE(false) << "equality check failed";
    PrintBytes("buf", buf);
    PrintBytes("exp", exp);
  }
}

}  // namespace (unnamed)
