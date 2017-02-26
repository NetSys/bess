// Internet checksum calculation/verification implementation
// for bytestream, IP, TCP, and incremental update of checksum

#ifndef BESS_UTILS_CHECKSUM_H_
#define BESS_UTILS_CHECKSUM_H_

#include <x86intrin.h>

#include <glog/logging.h>

#include "ip.h"
#include "tcp.h"

namespace bess {
namespace utils {

// All input bytestreams for checksum should be network-order
// Todo: strongly-typed endian for input/output paramters

// Return 32-bit one's complement sum of 'len' bytes from 'buf' and 'sum16'.
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
    sum64 += _mm_extract_epi64(sum128, 0) + _mm_extract_epi64(sum128, 1);
    buf64 = reinterpret_cast<const uint64_t *>(buf256);
  }
#endif

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

// Return internet checksum (the negative of 16-bit one's complement sum)
// of 'len' bytes from 'buf'
static inline uint16_t CalculateGenericChecksum(const void *buf, size_t len) {
  return FoldChecksum(CalculateSum(buf, len));
}

// Return true if the 'cksum' is correct for the 'len' bytes from 'buf'
static inline bool VerifyGenericChecksum(const void *buf, size_t len,
                                         uint16_t cksum) {
  uint16_t ret = CalculateGenericChecksum(buf, len);
  return ret == cksum;
}

// Return true if the 'len' bytes from 'buf' is correct
// Assumption: 'buf' already includes 16-bit checksum (e.g., IP/TCP header)
static inline bool VerifyGenericChecksum(const void *buf, size_t len) {
  return VerifyGenericChecksum(buf, len, 0);
}

// Return true if the IP checksum is true
static inline bool VerifyIpv4NoOptChecksum(const Ipv4Header &iph) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&iph);
  uint32_t sum = buf32[0];

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  // 2. reduce to 16-bit unsigned integer
  asm("addl %[u1], %[sum]   \n\t"
      "adcl %[u2], %[sum]   \n\t"
      "adcl %[u3], %[sum]   \n\t"
      "adcl %[u4], %[sum]   \n\t"
      "adcl $0, %[sum]        \n\t"
      : [sum] "+r"(sum)
      : [u1] "m"(buf32[1]), [u2] "m"(buf32[2]), [u3] "m"(buf32[3]),
        [u4] "m"(buf32[4]));

  return FoldChecksum(sum) == 0;
}

// Return IP checksum of the ip header 'iph' without ip options
// It skips the checksum field into the calculation
// It does not set the checksum field in ip header
static inline uint16_t CalculateIpv4NoOptChecksum(const Ipv4Header &iph) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&iph);
  uint32_t sum = buf32[0];

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  // 2. reduce to 16-bit unsigned integers
  // 3. negate
  asm("addl %[u1], %[sum]    \n\t"
      "adcl %[u2], %[sum]    \n\t"
      "adcl %[u3], %[sum]    \n\t"
      "adcl %[u4], %[sum]    \n\t"
      "adcl $0, %[sum]       \n\t"
      : [sum] "+r"(sum)
      : [u1] "m"(buf32[1]),
        [u2] "g"(buf32[2] & 0xFFFF),  // skip checksum fields
        [u3] "m"(buf32[3]), [u4] "m"(buf32[4]));

  return FoldChecksum(sum);
}

// Return true if the TCP checksum is true with the TCP header and
// pseudo header info - source ip, destiniation ip, and tcp byte stream length
// tcp_len: TCP header + payload in bytes
static inline bool VerifyIpv4TcpChecksum(const TcpHeader &tcph, uint32_t src_ip,
                                         uint32_t dst_ip, uint16_t tcp_len) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&tcph);

  // tcp options and data
  uint32_t sum = CalculateSum(buf32 + 5, tcp_len - 20);
  uint32_t len = static_cast<uint32_t>(htons(tcp_len));

  // Calculate the checksum of TCP pseudo header
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
        [u3] "m"(buf32[3]), [u4] "m"(buf32[4]), [src] "r"(src_ip),
        [dst] "r"(dst_ip), [len] "r"(len));

  return FoldChecksum(sum) == 0;
}

// Return true if the TCP checksum is true
static inline bool VerifyIpv4TcpChecksum(const Ipv4Header &iph,
                                         const TcpHeader &tcph) {
  return VerifyIpv4TcpChecksum(tcph, iph.src, iph.dst,
                               ntohs(iph.length) - (iph.header_length << 2));
}

// Return TCP (on IPv4) checksum of the tcp header 'tcph' with pseudo header
// informations - source ip ('src'), destiniation ip ('dst'),
// and tcp byte stream length ('tcp_len', tcp_header + data len)
// 'tcp_len' is in host-order, and the others are in network-order
// It skips the checksum field into the calculation
// It does not set the checksum field in TCP header
static inline uint16_t CalculateIpv4TcpChecksum(const TcpHeader &tcph,
                                                uint32_t src, uint32_t dst,
                                                uint16_t tcp_len) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&tcph);
  // tcp options and data
  uint32_t sum = CalculateSum(buf32 + 5, tcp_len - 20);
  uint32_t len = static_cast<uint32_t>(htons(tcp_len));

  // Calculate the checksum of TCP pseudo header
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
        [src] "r"(src), [dst] "r"(dst), [len] "r"(len));

  return FoldChecksum(sum);
}

// Return true if the TCP (on IPv4) checksum is true
static inline uint16_t CalculateIpv4TcpChecksum(const Ipv4Header &iph,
                                                const TcpHeader &tcph) {
  return CalculateIpv4TcpChecksum(tcph, iph.src, iph.dst,
                                  ntohs(iph.length) - (iph.header_length << 2));
}

// Return incrementally updated checksum from old_checksum
// when 32-bit 'old_value' changes to 'new_value' e.g., changed IPv4 address
static inline uint16_t CalculateChecksumIncremental32(uint16_t old_checksum,
                                                      uint32_t old_value,
                                                      uint32_t new_value) {
  // new checksum = ~(~old_checksum + ~old_value + new_value) by RFC 1624
  uint32_t sum = ~old_checksum & 0xFFFF;
  sum += (~old_value >> 16) + (~old_value & 0xFFFF);
  sum += (new_value >> 16) + (new_value & 0xFFFF);

  return FoldChecksum(sum);
}

// Return incrementally updated checksum from old_checksum
// when 16-bit 'old_value' changes to 'new_value' e.g., changed port number
static inline uint16_t CalculateChecksumIncremental16(uint16_t old_checksum,
                                                      uint16_t old_value,
                                                      uint16_t new_value) {
  // new checksum = ~(~old_checksum + ~old_value + new_value) by RFC 1624
  uint32_t sum = ~old_checksum & 0xFFFF;
  sum += ~old_value & 0xFFFF;
  sum += new_value;

  return FoldChecksum(sum);
}

}  // namespace utils
}  // namespace bess

#endif
