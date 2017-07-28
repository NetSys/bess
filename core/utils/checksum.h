// Copyright (c) 2014-2016, The Regents of the University of California.
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

// Internet checksum calculation/verification implementation
// for bytestream, IP, TCP, and incremental update of checksum

#ifndef BESS_UTILS_CHECKSUM_H_
#define BESS_UTILS_CHECKSUM_H_

#include <x86intrin.h>

#include "common.h"
#include "ip.h"
#include "simd.h"
#include "tcp.h"
#include "udp.h"

namespace bess {
namespace utils {

// All input bytestreams for checksum should be network-order
// Todo: strongly-typed endian for input/output paramters

// Returns 32-bit one's complement sum of 'len' bytes from 'buf' and 'sum16'.
static inline uint32_t CalculateSum(const void *buf, size_t len) {
  const uint64_t *buf64 = reinterpret_cast<const uint64_t *>(buf);
  uint64_t sum64 = 0;
  bool odd = len & 1;

#if __AVX2__
  // Faster for >128B data
  if (len >= sizeof(__m256i) * 4) {
    const __m256i *buf256 = reinterpret_cast<const __m256i *>(buf64);
    __m256i zero256 = _mm256_setzero_si256();

    // We parallelize two ymm streams to minimize register dependency:
    //     a: buf256,             buf256 + 2,             ...
    //     b:         buf256 + 1,             buf256 + 3, ...
    __m256i a = _mm256_loadu_si256(buf256);
    __m256i b = _mm256_loadu_si256(buf256 + 1);

    // For each stream, accumulate unpackhi and unpacklo in parallel
    // (as 4x64bit vectors, so that each upper 0000 can hold carries)
    // -------------------------------------------------------------------
    // 32B data: aaaaAAAA bbbbBBBB ccccCCCC ddddDDDD  (1 letter = 1 byte)
    // unpackhi: bbbb0000 BBBB0000 dddd0000 DDDD0000
    // unpacklo: aaaa0000 AAAA0000 cccc0000 CCCC0000
    __m256i sum_a_hi = _mm256_unpackhi_epi32(a, zero256);
    __m256i sum_a_lo = _mm256_unpacklo_epi32(a, zero256);
    __m256i sum_b_hi = _mm256_unpackhi_epi32(b, zero256);
    __m256i sum_b_lo = _mm256_unpacklo_epi32(b, zero256);

    len -= sizeof(__m256i) * 2;
    buf256 += 2;

    while (len >= sizeof(__m256i) * 2) {
      a = _mm256_loadu_si256(buf256);
      b = _mm256_loadu_si256(buf256 + 1);

      sum_a_hi = _mm256_add_epi64(sum_a_hi, _mm256_unpackhi_epi32(a, zero256));
      sum_a_lo = _mm256_add_epi64(sum_a_lo, _mm256_unpacklo_epi32(a, zero256));
      sum_b_hi = _mm256_add_epi64(sum_b_hi, _mm256_unpackhi_epi32(b, zero256));
      sum_b_lo = _mm256_add_epi64(sum_b_lo, _mm256_unpacklo_epi32(b, zero256));

      len -= sizeof(__m256i) * 2;
      buf256 += 2;
    }

    // fold four 256bit sums into one 128bit sum
    __m256i sum256 = _mm256_add_epi64(_mm256_add_epi64(sum_a_hi, sum_a_lo),
                                      _mm256_add_epi64(sum_b_hi, sum_b_lo));
    __m128i sum128 = _mm_add_epi64(_mm256_extracti128_si256(sum256, 0),
                                   _mm256_extracti128_si256(sum256, 1));

    // fold 128bit sum into 64bit
    sum64 += m128i_extract_u64(sum128, 0) + m128i_extract_u64(sum128, 1);
    buf64 = reinterpret_cast<const uint64_t *>(buf256);
  }
#endif

#if __x86_64
  // Repeat 64-bit one's complement sum (at sum64) including carrys
  // 8 additions in a loop
  while (len >= sizeof(uint64_t) * 8) {
    asm("addq %[u0], %[sum] \n\t"
        "adcq %[u1], %[sum] \n\t"
        "adcq %[u2], %[sum] \n\t"
        "adcq %[u3], %[sum] \n\t"
        "adcq %[u4], %[sum] \n\t"
        "adcq %[u5], %[sum] \n\t"
        "adcq %[u6], %[sum] \n\t"
        "adcq %[u7], %[sum] \n\t"
        "adcq $0, %[sum]"
        : [sum] "+r"(sum64)
        : [u0] "m"(buf64[0]), [u1] "m"(buf64[1]), [u2] "m"(buf64[2]),
          [u3] "m"(buf64[3]), [u4] "m"(buf64[4]), [u5] "m"(buf64[5]),
          [u6] "m"(buf64[6]), [u7] "m"(buf64[7]));
    len -= sizeof(uint64_t) * 8;
    buf64 += 8;
  }

  while (len >= sizeof(uint64_t) * 2) {
    // Repeat 64-bit one's complement sum (at sum64) including carrys
    // 2 additions in a loop
    asm("addq %[u0], %[sum] \n\t"
        "adcq %[u1], %[sum] \n\t"
        "adcq $0, %[sum]"
        : [sum] "+r"(sum64)
        : [u0] "m"(buf64[0]), [u1] "m"(buf64[1]));
    len -= sizeof(uint64_t) * 2;
    buf64 += 2;
  }

  // Reduce 64-bit unsigned integer to 32-bit unsigned integer
  // Carry may happens, but no need to complete reduce here
  sum64 = (sum64 >> 32) + (sum64 & 0xFFFFFFFF);
#else
  // Use stantard C language for 32 bit or other non-Intel
  typedef union[[gnu::may_alias]] {
    uint32_t u64;
    uint16_t u16[4];
  }
  u16_64;
  const u16_64 *ubuf64;
  ubuf64 = reinterpret_cast<const u16_64 *>(buf64);
  while (len >= sizeof(uint64_t)) {
    sum64 += ubuf64->u16[0];
    sum64 += ubuf64->u16[1];
    sum64 += ubuf64->u16[2];
    sum64 += ubuf64->u16[3];
    len -= sizeof(uint64_t);
    ubuf64++;
  }
  buf64 = reinterpret_cast<const uint64_t *>(ubuf64);
#endif

  // Repeat 16-bit one's complement sum (at sum64)
  const uint16_t *buf16 = reinterpret_cast<const uint16_t *>(buf64);
  while (len >= sizeof(uint16_t)) {
    sum64 += *buf16++;
    len -= sizeof(uint16_t);
  }

  // Add remaining 8-bit to the one's complement sum
  if (odd) {
    sum64 += *reinterpret_cast<const uint8_t *>(buf16);
  }

  // Reduce 64-bit unsigned int to 32-bit unsigned int
  sum64 = (sum64 >> 32) + (sum64 & 0xFFFFFFFF);
  sum64 += (sum64 >> 32);

  return static_cast<uint32_t>(sum64);
}

// Fold a 32-bit non-inverted checksum into a inverted 16-bit one,
// which can be readily written to L3/L4 checksum field
static inline uint16_t FoldChecksum(uint32_t cksum) {
  cksum = (cksum >> 16) + (cksum & 0xFFFF);
  cksum += (cksum >> 16);
  return ~cksum;
}

// Returns internet checksum (the negative of 16-bit one's complement sum)
// of 'len' bytes from 'buf'
static inline uint16_t CalculateGenericChecksum(const void *buf, size_t len) {
  return FoldChecksum(CalculateSum(buf, len));
}

// Returns true if the 'cksum' is correct for the 'len' bytes from 'buf'
static inline bool VerifyGenericChecksum(const void *buf, size_t len,
                                         uint16_t cksum) {
  uint16_t ret = CalculateGenericChecksum(buf, len);
  return ret == cksum;
}

// Returns true if the 'len' bytes from 'buf' is correct
// Assumption: 'buf' already includes 16-bit checksum (e.g., IP/TCP header)
static inline bool VerifyGenericChecksum(const void *buf, size_t len) {
  return VerifyGenericChecksum(buf, len, 0);
}

// Returns true if the IP checksum is correct
static inline bool VerifyIpv4NoOptChecksum(const Ipv4 &iph) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&iph);
  uint32_t sum = buf32[0];

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  asm("addl %[u1], %[sum]   \n\t"
      "adcl %[u2], %[sum]   \n\t"
      "adcl %[u3], %[sum]   \n\t"
      "adcl %[u4], %[sum]   \n\t"
      "adcl $0, %[sum]        \n\t"
      : [sum] "+r"(sum)
      : [u1] "m"(buf32[1]), [u2] "m"(buf32[2]), [u3] "m"(buf32[3]),
        [u4] "m"(buf32[4]));

  // 2. reduce to 16-bit unsigned integer and negate
  return FoldChecksum(sum) == 0;
}

// Returns IP checksum of the ip header 'iph' without ip options
// It skips the checksum field into the calculation
// It does not set the checksum field in ip header
static inline uint16_t CalculateIpv4NoOptChecksum(const Ipv4 &iph) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&iph);
  uint32_t sum = buf32[0];

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  asm("addl %[u1], %[sum]    \n\t"
      "adcl %[u2], %[sum]    \n\t"
      "adcl %[u3], %[sum]    \n\t"
      "adcl %[u4], %[sum]    \n\t"
      "adcl $0, %[sum]       \n\t"
      : [sum] "+r"(sum)
      : [u1] "m"(buf32[1]),
        [u2] "g"(buf32[2] & 0xFFFF),  // skip checksum fields
        [u3] "m"(buf32[3]), [u4] "m"(buf32[4]));

  // 2. reduce to 16-bit unsigned integer and negate
  return FoldChecksum(sum);
}

// Returns true if the IP checksum is correct
static inline bool VerifyIpv4Checksum(const Ipv4 &iph) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&iph);
  size_t ip_header_len = iph.header_length << 2;

  if (likely(ip_header_len == sizeof(iph))) {
    return VerifyIpv4NoOptChecksum(iph);
  }

  if (unlikely(ip_header_len < sizeof(iph))) {
    return false;  // Invalid IP header
  }

  uint32_t sum = CalculateSum(buf32 + sizeof(iph) / sizeof(*buf32),
                              ip_header_len - sizeof(iph));

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  asm("addl %[u0], %[sum]   \n\t"
      "adcl %[u1], %[sum]   \n\t"
      "adcl %[u2], %[sum]   \n\t"
      "adcl %[u3], %[sum]   \n\t"
      "adcl %[u4], %[sum]   \n\t"
      "adcl $0, %[sum]        \n\t"
      : [sum] "+r"(sum)
      : [u0] "m"(buf32[0]), [u1] "m"(buf32[1]), [u2] "m"(buf32[2]),
        [u3] "m"(buf32[3]), [u4] "m"(buf32[4]));

  // 2. reduce to 16-bit unsigned integer and negate
  return FoldChecksum(sum) == 0;
}

// Returns IP checksum of the ip header 'iph'
// It skips the checksum field into the calculation
// It does not set the checksum field in ip header
static inline uint16_t CalculateIpv4Checksum(const Ipv4 &iph) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&iph);
  size_t ip_header_len = iph.header_length << 2;

  if (likely(ip_header_len == sizeof(iph))) {
    return CalculateIpv4NoOptChecksum(iph);
  }

  if (unlikely(ip_header_len < sizeof(iph))) {
    return 0;  // Invalid IP header. Give up.
  }

  uint32_t sum = CalculateSum(buf32 + sizeof(iph) / sizeof(*buf32),
                              ip_header_len - sizeof(iph));

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  asm("addl %[u0], %[sum]    \n\t"
      "adcl %[u1], %[sum]    \n\t"
      "adcl %[u2], %[sum]    \n\t"
      "adcl %[u3], %[sum]    \n\t"
      "adcl %[u4], %[sum]    \n\t"
      "adcl $0, %[sum]       \n\t"
      : [sum] "+r"(sum)
      : [u0] "m"(buf32[0]), [u1] "m"(buf32[1]),
        [u2] "g"(buf32[2] & 0xFFFF),  // skip checksum fields
        [u3] "m"(buf32[3]), [u4] "m"(buf32[4]));

  // 2. reduce to 16-bit unsigned integer and negate
  return FoldChecksum(sum);
}

// Returns true if the UDP checksum is correct with the UDP header and
// pseudo header info - source ip, destiniation ip, and UDP byte stream length
// udp_len: UDP header + payload in bytes.
// NOTE: Undefined behavior if udp_len < 8
static inline bool VerifyIpv4UdpChecksum(const Udp &udph, be32_t src_ip,
                                         be32_t dst_ip, uint16_t udp_len) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&udph);

  // UDP checksum is optional, and all zeroes mean "not computed"
  if (udph.checksum == 0) {
    return true;
  }

  // UDP payload
  uint32_t sum = CalculateSum(buf32 + sizeof(udph) / sizeof(*buf32),
                              udp_len - sizeof(udph));
  uint32_t len = static_cast<uint32_t>(be16_t::swap(udp_len));

  // Calculate the checksum of UDP header and pseudo header
  asm("addl %[u0], %[sum]      \n\t"
      "adcl %[u1], %[sum]      \n\t"
      "adcl %[src], %[sum]     \n\t"
      "adcl %[dst], %[sum]     \n\t"
      "adcl %[len], %[sum]     \n\t"
      "adcl $0x1100, %[sum]    \n\t"  // 17 == IPPROTO_UDP
      "adcl $0, %[sum]         \n\t"
      : [sum] "+r"(sum)
      : [u0] "m"(buf32[0]), [u1] "m"(buf32[1]), [src] "r"(src_ip.raw_value()),
        [dst] "r"(dst_ip.raw_value()), [len] "r"(len));

  return FoldChecksum(sum) == 0;
}

// Returns true if the UDP checksum is correct
static inline bool VerifyIpv4UdpChecksum(const Ipv4 &iph, const Udp &udph) {
  size_t udp_len = udph.length.value();

  if (unlikely(udp_len < sizeof(udph))) {
    return false;  // Invalid UDP header
  }

  return VerifyIpv4UdpChecksum(udph, iph.src, iph.dst, udp_len);
}

// Returns UDP (on IPv4) checksum of the UDP header 'udph' with pseudo header
// informations - source ip ('src'), destiniation ip ('dst'),
// and UDP byte stream length ('udp_len', udp_header + payload len)
// 'udp_len' is in host-order, and the others are in network-order
// It skips the checksum field into the calculation
// It does not set the checksum field in UDP header
// NOTE: Undefined behavior if udp_len < 8
static inline uint16_t CalculateIpv4UdpChecksum(const Udp &udph, be32_t src,
                                                be32_t dst, uint16_t udp_len) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&udph);
  // UDP payload
  uint32_t sum = CalculateSum(buf32 + sizeof(udph) / sizeof(*buf32),
                              udp_len - sizeof(udph));
  uint32_t len = static_cast<uint32_t>(be16_t::swap(udp_len));

  // Calculate the checksum of UDP header and pseudo header
  asm("addl %[u0], %[sum]      \n\t"
      "adcl %[u1], %[sum]      \n\t"
      "adcl %[src], %[sum]     \n\t"
      "adcl %[dst], %[sum]     \n\t"
      "adcl %[len], %[sum]     \n\t"
      "adcl $0x1100, %[sum]    \n\t"  // 17 == IPPROTO_UDP
      "adcl $0, %[sum]         \n\t"
      : [sum] "+r"(sum)
      : [u0] "m"(buf32[0]), [u1] "g"(buf32[1] & 0xFFFF),  // skip checksum field
        [src] "r"(src.raw_value()), [dst] "r"(dst.raw_value()), [len] "r"(len));

  // If the result of UDP checksum calculation is 0, return all ones (rfc 768)
  return FoldChecksum(sum) ?: 0xFFFF;
}

// Returns UDP (on IPv4) checksum of the UDP header 'udph' with ip header 'iph'
// It skips the checksum field into the calculation
// It does not set the checksum field in UDP header
static inline uint16_t CalculateIpv4UdpChecksum(const Ipv4 &iph,
                                                const Udp &udph) {
  size_t udp_len = udph.length.value();

  if (unlikely(udp_len < sizeof(udph))) {
    return 0;
  }

  return CalculateIpv4UdpChecksum(udph, iph.src, iph.dst, udp_len);
}

// Returns true if the TCP checksum is correct with the TCP header and
// pseudo header info - source ip, destiniation ip, and tcp byte stream length
// tcp_len: TCP header + payload in bytes
// NOTE: Undefined behavior if tcp_len < 20
static inline bool VerifyIpv4TcpChecksum(const Tcp &tcph, be32_t src_ip,
                                         be32_t dst_ip, uint16_t tcp_len) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&tcph);

  // TCP options and payload
  uint32_t sum = CalculateSum(buf32 + sizeof(tcph) / sizeof(*buf32),
                              tcp_len - sizeof(tcph));
  uint32_t len = static_cast<uint32_t>(be16_t::swap(tcp_len));

  // Calculate the checksum of TCP header and pseudo header
  asm("addl %[u0], %[sum]      \n\t"
      "adcl %[u1], %[sum]      \n\t"
      "adcl %[u2], %[sum]      \n\t"
      "adcl %[u3], %[sum]      \n\t"
      "adcl %[u4], %[sum]      \n\t"
      "adcl %[src], %[sum]     \n\t"
      "adcl %[dst], %[sum]     \n\t"
      "adcl %[len], %[sum]     \n\t"
      "adcl $0x0600, %[sum]    \n\t"  // 6 == IPPROTO_TCP
      "adcl $0, %[sum]         \n\t"
      : [sum] "+r"(sum)
      : [u0] "m"(buf32[0]), [u1] "m"(buf32[1]), [u2] "m"(buf32[2]),
        [u3] "m"(buf32[3]), [u4] "m"(buf32[4]), [src] "r"(src_ip.raw_value()),
        [dst] "r"(dst_ip.raw_value()), [len] "r"(len));

  return FoldChecksum(sum) == 0;
}

// Returns true if the TCP checksum is correct
static inline bool VerifyIpv4TcpChecksum(const Ipv4 &iph, const Tcp &tcph) {
  // Unlike UDP, TCP doesn't have a length field. Derive from IP header.
  size_t ip_len = iph.length.value();
  size_t ip_header_len = iph.header_length << 2;

  if (unlikely(ip_len < ip_header_len + sizeof(tcph))) {
    return false;  // Invalid IP header
  }

  return VerifyIpv4TcpChecksum(tcph, iph.src, iph.dst, ip_len - ip_header_len);
}

// Returns TCP (on IPv4) checksum of the tcp header 'tcph' with pseudo header
// informations - source ip ('src'), destiniation ip ('dst'),
// and tcp byte stream length ('tcp_len', tcp_header + payload len)
// 'tcp_len' is in host-order, and the others are in network-order
// It skips the checksum field into the calculation
// It does not set the checksum field in TCP header
// NOTE: Undefined behavior if tcp_len < 20
static inline uint16_t CalculateIpv4TcpChecksum(const Tcp &tcph, be32_t src,
                                                be32_t dst, uint16_t tcp_len) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&tcph);
  // tcp options and payload
  uint32_t sum = CalculateSum(buf32 + sizeof(tcph) / sizeof(*buf32),
                              tcp_len - sizeof(tcph));
  uint32_t len = static_cast<uint32_t>(be16_t::swap(tcp_len));

  // Calculate the checksum of TCP header and pseudo header
  asm("addl %[u0], %[sum]      \n\t"
      "adcl %[u1], %[sum]      \n\t"
      "adcl %[u2], %[sum]      \n\t"
      "adcl %[u3], %[sum]      \n\t"
      "adcl %[u4], %[sum]      \n\t"
      "adcl %[src], %[sum]     \n\t"
      "adcl %[dst], %[sum]     \n\t"
      "adcl %[len], %[sum]     \n\t"
      "adcl $0x0600, %[sum]    \n\t"  // 6 == IPPROTO_TCP
      "adcl $0, %[sum]         \n\t"
      : [sum] "+r"(sum)
      : [u0] "m"(buf32[0]), [u1] "m"(buf32[1]), [u2] "m"(buf32[2]),
        [u3] "m"(buf32[3]),
        [u4] "g"(buf32[4] >> 16),  // skip checksum field
        [src] "r"(src.raw_value()), [dst] "r"(dst.raw_value()), [len] "r"(len));

  return FoldChecksum(sum);
}

// Returns TCP (on IPv4) checksum of the tcp header 'tcph' with ip header 'iph'
// It skips the checksum field into the calculation
// It does not set the checksum field in TCP header
static inline uint16_t CalculateIpv4TcpChecksum(const Ipv4 &iph,
                                                const Tcp &tcph) {
  // Unlike UDP, TCP doesn't have a length field. Derive from IP header.
  size_t ip_len = iph.length.value();
  size_t ip_header_len = iph.header_length << 2;

  if (unlikely(ip_len < ip_header_len + sizeof(tcph))) {
    return 0;  // Invalid IP header
  }

  return CalculateIpv4TcpChecksum(tcph, iph.src, iph.dst,
                                  ip_len - ip_header_len);
}

// Incremental checksum update
//
// The functions below can be used to update multiple fields and update the
// checksum in a single shot:
//
// uint32_t increment = 0;
//
// increment += ChecksumIncrement32(iphdr->src, new_src);
// increment += ChecksumIncrement32(iphdr->dst, new_dst);
//
// iphdr->src = new_src
// iphdr->dst = new_dst
// iphdr->checksum = UpdateChecksumWithIncrement(iphdr->checksum, incremental);

static inline uint32_t ChecksumIncrement32(uint32_t old_value,
                                           uint32_t new_value) {
  uint32_t sum = (~old_value >> 16) + (~old_value & 0xFFFF);
  sum += (new_value >> 16) + (new_value & 0xFFFF);
  return sum;
}

// Note that the return type is uint32_t. You can add up increments from both
// ChecksumIncrement16() and ChecksumIncrement32()
static inline uint32_t ChecksumIncrement16(uint16_t old_value,
                                           uint16_t new_value) {
  return (~old_value & 0xFFFF) + new_value;
}

// Returns updated checksum value, which is ready to be written in the header
static inline uint16_t UpdateChecksumWithIncrement(uint16_t old_checksum,
                                                   uint32_t increment) {
  return FoldChecksum((~old_checksum & 0xFFFF) + increment);
}

// Returns incrementally updated checksum from old_checksum
// when 32-bit 'old_value' changes to 'new_value' e.g., changed IPv4 address
static inline uint16_t UpdateChecksum32(uint16_t old_checksum,
                                        uint32_t old_value,
                                        uint32_t new_value) {
  // new checksum = ~(~old_checksum + ~old_value + new_value) by RFC 1624
  uint32_t inc = ChecksumIncrement32(old_value, new_value);

  return UpdateChecksumWithIncrement(old_checksum, inc);
}

// Returns incrementally updated checksum from old_checksum
// when 16-bit 'old_value' changes to 'new_value' e.g., changed port number
static inline uint16_t UpdateChecksum16(uint16_t old_checksum,
                                        uint16_t old_value,
                                        uint16_t new_value) {
  // new checksum = ~(~old_checksum + ~old_value + new_value) by RFC 1624
  uint32_t inc = ChecksumIncrement16(old_value, new_value);

  return UpdateChecksumWithIncrement(old_checksum, inc);
}

}  // namespace utils
}  // namespace bess

#endif
