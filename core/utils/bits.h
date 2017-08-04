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

#ifndef BESS_UTILS_BITS_H_
#define BESS_UTILS_BITS_H_

#include <glog/logging.h>
#include <x86intrin.h>

#include <algorithm>

namespace bess {
namespace utils {

// TODO(melvinw): add support for shifting at bit granularity
// Shifts `buf` to the left by `len` bytes and fills in with zeroes using
// std::memmove() and std::memset().
static inline void ShiftBytesLeftSmall(uint8_t *buf, const size_t len,
                                       size_t shift) {
  shift = std::min(shift, len);
  memmove(buf, buf + shift, len - shift);
  memset(buf + len - shift, 0, shift);
}

// TODO(melvinw): add support for shifting at bit granularity
// Shifts `buf` to the left by `len` bytes and fills in with zeroes.
// Will shift in 8-byte chunks if `shift` <= 8, othersize, uses
// std::memmove() and std::memset().
static inline void ShiftBytesLeft(uint8_t *buf, const size_t len,
                                  const size_t shift) {
  if (len < sizeof(uint64_t) || shift > sizeof(uint64_t)) {
    return ShiftBytesLeftSmall(buf, len, shift);
  }

  uint8_t *tmp_buf = buf;
  size_t tmp_len = len;
  size_t inc = sizeof(uint64_t) - shift;
  while (tmp_len >= sizeof(uint64_t)) {
    uint64_t *block = reinterpret_cast<uint64_t *>(tmp_buf);
    *block >>= shift * 8;
    tmp_buf += inc;
    tmp_len = buf + len - tmp_buf;
  }

  buf += len;
  if (static_cast<size_t>(buf - tmp_buf) > shift) {
    tmp_len = buf - tmp_buf - shift;
    memmove(tmp_buf, tmp_buf + shift, tmp_len);
    memset(buf - shift, 0, shift);
  }
}

// TODO(melvinw): add support for shifting at bit granularity
// Shifts `buf` to the right by `len` bytes and fills in with zeroes using
// std::memmove() and std::memset().
static inline void ShiftBytesRightSmall(uint8_t *buf, const size_t len,
                                        size_t shift) {
  shift = std::min(shift, len);
  memmove(buf + shift, buf, len - shift);
  memset(buf, 0, shift);
}

// TODO(melvinw): add support for shifting at bit granularity
// Shifts `buf` to the right by `len` bytes and fills in with zeroes.
// Will shift in 8-byte chunks if `shift` <= 8, othersize, uses
// std::memmove() and std::memset().
static inline void ShiftBytesRight(uint8_t *buf, const size_t len,
                                   const size_t shift) {
  if (len < sizeof(uint64_t) || shift > sizeof(uint64_t)) {
    return ShiftBytesRightSmall(buf, len, shift);
  }

  uint8_t *tmp_buf = buf + len - sizeof(uint64_t);
  size_t dec = sizeof(uint64_t) - shift;
  size_t leftover = len;
  while (tmp_buf >= buf) {
    uint64_t *block = reinterpret_cast<uint64_t *>(tmp_buf);
    *block <<= shift * 8;
    tmp_buf -= dec;
    leftover -= dec;
  }
  ShiftBytesRightSmall(buf, leftover, shift);
}

// Applies the `len`-byte bitmask `mask` to `buf`, in 1-byte chunks.
static inline void MaskBytesSmall(uint8_t *buf, const uint8_t *mask,
                                  const size_t len) {
  for (size_t i = 0; i < len; i++) {
    buf[i] &= mask[i];
  }
}

// Applies the `len`-byte bitmask `mask` to `buf`, in 8-byte chunks if able,
// otherwise, falls back to 1-byte chunks.
static inline void MaskBytes64(uint8_t *buf, uint8_t const *mask,
                               const size_t len) {
  size_t n = len / sizeof(uint64_t);
  size_t leftover = len - n * sizeof(uint64_t);
  uint64_t *buf64 = reinterpret_cast<uint64_t *>(buf);
  const uint64_t *mask64 = reinterpret_cast<const uint64_t *>(mask);
  for (size_t i = 0; i < n; i++) {
    buf64[i] &= mask64[i];
  }

  if (leftover) {
    buf = reinterpret_cast<uint8_t *>(buf64 + n);
    mask = reinterpret_cast<uint8_t const *>(mask64 + n);
    MaskBytesSmall(buf, mask, leftover);
  }
}

// Applies the `len`-byte bitmask `mask` to `buf`, in 16-byte chunks if able,
// otherwise, falls back to 8-byte chunks and possibly 1-byte chunks.
static inline void MaskBytes(uint8_t *buf, uint8_t const *mask,
                             const size_t len) {
  if (len <= sizeof(uint64_t)) {
    return MaskBytes64(buf, mask, len);
  }

  // AVX2?
  size_t n = len / sizeof(__m128i);
  size_t leftover = len - n * sizeof(__m128i);
  __m128i *buf128 = reinterpret_cast<__m128i *>(buf);
  const __m128i *mask128 = reinterpret_cast<const __m128i *>(mask);
  for (size_t i = 0; i < n; i++) {
    __m128i a = _mm_loadu_si128(buf128 + i);
    __m128i b = _mm_loadu_si128(mask128 + i);
    _mm_storeu_si128(buf128 + i, _mm_and_si128(a, b));
  }

  buf = reinterpret_cast<uint8_t *>(buf128 + n);
  mask = reinterpret_cast<uint8_t const *>(mask128 + n);
  if (leftover >= sizeof(uint64_t)) {
    MaskBytes64(buf, mask, leftover);
  } else {
    MaskBytesSmall(buf, mask, leftover);
  }
}

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_BITS_H_
