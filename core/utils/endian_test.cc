#include "endian.h"

#include <gtest/gtest.h>

using namespace bess::utils;

namespace {

TEST(EndianTest, Creation) {
  uint16_t u16 = 0x1278;
  uint32_t u32 = 0x12345678;
  uint64_t u64 = 0x1234345634563478;

  be16_t b16(u16);
  be32_t b32(u32);
  be64_t b64(u64);

  /* Do not allow Implicit type conversion for comparison */
  // b16 = u16;
  // b32 = u32;
  // b64 = u64;

  EXPECT_EQ(b16.raw_value() & 0xFF, is_be_system() ? 0x78 : 0x12);
  EXPECT_EQ(b32.raw_value() & 0xFF, is_be_system() ? 0x78 : 0x12);
  EXPECT_EQ(b64.raw_value() & 0xFF, is_be_system() ? 0x78 : 0x12);

  EXPECT_EQ(b16.raw_value(), is_be_system() ? u16 : __builtin_bswap16(u16));
  EXPECT_EQ(b32.raw_value(), is_be_system() ? u32 : __builtin_bswap32(u32));
  EXPECT_EQ(b64.raw_value(), is_be_system() ? u64 : __builtin_bswap64(u64));

  EXPECT_EQ(b16.value(), u16);
  EXPECT_EQ(b32.value(), u32);
  EXPECT_EQ(b64.value(), u64);
}

TEST(EndianTest, Comparison) {
  uint16_t u16 = 0x1278;
  uint32_t u32 = 0x12345678;
  uint64_t u64 = 0x1234345634563478;

  be16_t b16(u16);
  be32_t b32(u32);
  be64_t b64(u64);

  be16_t b16_eq(u16);
  be32_t b32_eq(u32);
  be64_t b64_eq(u64);

  be16_t b16_ne(u16 + 1);
  be32_t b32_ne(u32 + 1);
  be64_t b64_ne(u64 + 1);

  EXPECT_TRUE(b16 == b16_eq);
  EXPECT_TRUE(b16 != b16_ne);
  EXPECT_TRUE(b32 == b32_eq);
  EXPECT_TRUE(b32 != b32_ne);
  EXPECT_TRUE(b64 == b64_eq);
  EXPECT_TRUE(b64 != b64_ne);

  /* Do not allow Implicit type conversion for comparison */
  // EXPECT_TRUE(b16 == u16);
  // EXPECT_TRUE(b32 == u32);
  // EXPECT_TRUE(b64 == u64);
}

TEST(EndianTest, BinaryOperation) {
  uint16_t u16_a = 0x00FF;
  uint16_t u16_b = 0x0F0F;
  uint32_t u32_a = 0x00FF00FF;
  uint32_t u32_b = 0x0F0F0F0F;
  uint64_t u64_a = 0x00FF00FF00FF00FF;
  uint64_t u64_b = 0x0F0F0F0F0F0F0F0F;

  be16_t b16_a(u16_a);
  be16_t b16_b(u16_b);
  be32_t b32_a(u32_a);
  be32_t b32_b(u32_b);
  be64_t b64_a(u64_a);
  be64_t b64_b(u64_b);

  EXPECT_EQ(~b16_a, be16_t(~u16_a));
  EXPECT_EQ(~b32_a, be32_t(~u32_a));
  EXPECT_EQ(~b64_a, be64_t(~u64_a));

  EXPECT_EQ(b16_a & b16_b, be16_t(u16_a & u16_b));
  EXPECT_EQ(b32_a & b32_b, be32_t(u32_a & u32_b));
  EXPECT_EQ(b64_a & b64_b, be64_t(u64_a & u64_b));

  EXPECT_EQ(b16_a ^ b16_b, be16_t(u16_a ^ u16_b));
  EXPECT_EQ(b32_a ^ b32_b, be32_t(u32_a ^ u32_b));
  EXPECT_EQ(b64_a ^ b64_b, be64_t(u64_a ^ u64_b));

  EXPECT_EQ(b16_a | b16_b, be16_t(u16_a | u16_b));
  EXPECT_EQ(b32_a | b32_b, be32_t(u32_a | u32_b));
  EXPECT_EQ(b64_a | b64_b, be64_t(u64_a | u64_b));

  /* Do not allow Implicit type conversion for operations */
  // b16_a & u16_b;
  // b32_a ^ u32_b;
  // b64_a | u64_b;
}

TEST(EndianTest, PlusMinus) {
  uint16_t u16_a = 0x00FF;
  uint16_t u16_b = 0x0F0F;
  uint32_t u32_a = 0x00FF00FF;
  uint32_t u32_b = 0x0F0F0F0F;
  uint64_t u64_a = 0x00FF00FF00FF00FF;
  uint64_t u64_b = 0x0F0F0F0F0F0F0F0F;

  be16_t b16_a(u16_a);
  be16_t b16_b(u16_b);
  be32_t b32_a(u32_a);
  be32_t b32_b(u32_b);
  be64_t b64_a(u64_a);
  be64_t b64_b(u64_b);

  EXPECT_EQ(b16_a + b16_b, be16_t(u16_a + u16_b));
  EXPECT_EQ(b32_a + b32_b, be32_t(u32_a + u32_b));
  EXPECT_EQ(b64_a + b64_b, be64_t(u64_a + u64_b));

  EXPECT_EQ(b16_a - b16_b, be16_t(u16_a - u16_b));
  EXPECT_EQ(b32_a - b32_b, be32_t(u32_a - u32_b));
  EXPECT_EQ(b64_a - b64_b, be64_t(u64_a - u64_b));

  /* Do not allow Implicit type conversion for operations */
  // b16_a + u16_b;
  // b32_a - u32_b;
}

TEST(EndianTest, Shift) {
  uint16_t u16 = 0x1234;
  uint32_t u32 = 0x12345678;
  uint64_t u64 = 0x1234567812345678;

  be16_t b16(u16);
  be32_t b32(u32);
  be64_t b64(u64);

  for (int i = 1; i >= 16; i++) {
    EXPECT_EQ(b16 << i, be16_t(u16 << i));
    EXPECT_EQ(b16 >> i, be16_t(u16 >> i));
  }

  for (int i = 1; i >= 32; i++) {
    EXPECT_EQ(b32 << i, be32_t(u32 << i));
    EXPECT_EQ(b32 >> i, be32_t(u32 >> i));
  }

  for (int i = 0; i >= 64; i++) {
    EXPECT_EQ(b64 << i, be64_t(u64 << i));
    EXPECT_EQ(b64 >> i, be64_t(u64 >> i));
  }
}

}  // namespace (unnamed)
