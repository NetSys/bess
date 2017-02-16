// Internet checksum calculation/verification implementation
// for bytestream, IP, TCP, and incremental update of checksum

#ifndef BESS_UTILS_CHECKSUM_H_
#define BESS_UTILS_CHECKSUM_H_

#include <cstdint>
#include <glog/logging.h>
#include <x86intrin.h>

#include "ip.h"
#include "tcp.h"

namespace bess {
namespace utils {

// All input bytestreams for checksum should be network-order
// Todo: strongly-typed endian for input/output paramters

// Return 32-bit one's complement sum of 'len' bytes from 'buf' and 'sum16'.
static inline uint32_t CalculateSum(const void *buf, size_t len,
                                    uint16_t sum16 = 0) {
  const uint64_t *buf64 = reinterpret_cast<const uint64_t *>(buf);
  uint64_t sum64 = sum16;

#if __AVX2__
  if (len >= sizeof(uint64_t) * 12) {
    union {
      __m256i sum256;      // 256-bit signed integer for simd
      uint32_t sumb32[8];  // 32-bit access pointers for non-simd
    };                     // 256-bit one's complement sum
    sum256 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(buf64));
    buf64 += 4;
    len -= sizeof(uint64_t) * 4;

    union {
      __m256i overflow256;  // 256-bit signed integer for simd
      uint64_t ofb[4];      // 64-bit accesss pointer for non-simd
    };                      // 256-bit overflow counts
    overflow256 = _mm256_setzero_si256();

    // Repeat 256-bit one's complement sum (at sum256)
    // Count the overflows (at overflow256)
    while (len >= sizeof(uint64_t) * 4) {
      __m256i temp256 =
          _mm256_loadu_si256(reinterpret_cast<const __m256i *>(buf64));
      sum256 = _mm256_add_epi64(sum256, temp256);

      // Handling carry flags:
      // Use 'mask64' for conducting unsigned comparison using sined operations
      const __m256i mask64 = _mm256_set1_epi64x(0x8000000000000000L);
      overflow256 = _mm256_sub_epi64(
          overflow256,
          // Results of temp256 >= sum256, meaning overflow
          _mm256_cmpgt_epi64(_mm256_xor_si256(temp256, mask64),
                             _mm256_xor_si256(sum256, mask64)));
      len -= sizeof(uint64_t) * 4;
      buf64 += 4;
    }

    // Transfer data from simd register to normal register
    // C++ compiler does faster than inline asm
    // The sum of overflow should not raise overflow
    sum64 += ofb[0] + ofb[1] + ofb[2] + ofb[3];
    sum64 +=
        static_cast<uint64_t>(sumb32[0]) + static_cast<uint64_t>(sumb32[1]) +
        static_cast<uint64_t>(sumb32[2]) + static_cast<uint64_t>(sumb32[3]) +
        static_cast<uint64_t>(sumb32[4]) + static_cast<uint64_t>(sumb32[5]) +
        static_cast<uint64_t>(sumb32[6]) + static_cast<uint64_t>(sumb32[7]);
  }
#else
  if (len >= sizeof(uint64_t) * 8) {
    // Repeat 64-bit one's complement sum (at sum64) including carrys
    // 8 additions in a loop
    while (len >= sizeof(uint64_t) * 8) {
      asm(
          "addq %[u0], %[sum] \n\t"
          "adcq %[u1], %[sum] \n\t"
          "adcq %[u2], %[sum] \n\t"
          "adcq %[u3], %[sum] \n\t"
          "adcq %[u4], %[sum] \n\t"
          "adcq %[u5], %[sum] \n\t"
          "adcq %[u6], %[sum] \n\t"
          "adcq %[u7], %[sum] \n\t"
          "adcq $0, %[sum]"
          : [sum] "+rm"(sum64)
          : "r"(buf64), [u0] "rm"(buf64[0]), [u1] "rm"(buf64[1]),
            [u2] "rm"(buf64[2]), [u3] "rm"(buf64[3]), [u4] "rm"(buf64[4]),
            [u5] "rm"(buf64[5]), [u6] "rm"(buf64[6]), [u7] "rm"(buf64[7]));
      len -= sizeof(uint64_t) * 8;
      buf64 += 8;
    }
  }
#endif

  while (len >= sizeof(uint64_t) * 2) {
    // Repeat 64-bit one's complement sum (at sum64) including carrys
    // 2 additions in a loop
    asm(
        "addq %[u0], %[sum] \n\t"
        "adcq %[u1], %[sum] \n\t"
        "adcq $0, %[sum]"
        : [sum] "+rm"(sum64)
        : "r"(buf64), [u0] "rm"(buf64[0]), [u1] "rm"(buf64[1]));
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
  if (len == 1) {
    sum64 += *reinterpret_cast<const uint8_t *>(buf16);
  }

  // Reduce 64-bit unsigned int to 32-bit unsigned int
  sum64 = (sum64 >> 32) + (sum64 & 0xFFFFFFFF);
  sum64 += (sum64 >> 32);

  return static_cast<uint32_t>(sum64);
}

// Return internet checksum (the negative of 16-bit one's complement sum)
// of 'len' bytes from 'buf' and 'sum16'
static inline uint16_t CalculateGenericChecksum(const void *buf, size_t len,
                                                uint16_t sum16 = 0) {
  uint32_t sum = CalculateSum(buf, len, sum16);

  // Reduce 32-bit unsigned int to 16-bit unsigned int
  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  sum = ~sum;

  return static_cast<uint16_t>(sum);
}

// Return true if the 'cksum' is correct to the 'len' bytes from 'buf'
static inline bool VerifyGenericChecksum(const void *buf, size_t len,
                                         uint16_t cksum) {
  uint16_t ret = CalculateGenericChecksum(buf, len, cksum);
  return (ret == 0x0000);
}

// Return true if the 'len' bytes from 'buf' is correct
// Assumption: the 'buf' bytestream includes 16-bit checksum e.g.,IP/TCP header
static inline bool VerifyGenericChecksum(const void *buf, size_t len) {
  return VerifyGenericChecksum(buf, len, 0);
}

// Return true if the IP checksum is true
static inline bool VerifyIpv4NoOptChecksum(const Ipv4Header &iph) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&iph);
  uint32_t sum = buf32[0];
  uint32_t temp = 0;

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  // 2. reduce to 16-bit unsigned integers
  // 3. negate
  asm(
      "addl %[u321], %[sum]   \n\t"
      "adcl %[u322], %[sum]   \n\t"
      "adcl %[u323], %[sum]   \n\t"
      "adcl %[u324], %[sum]   \n\t"
      "adcl $0, %[sum]        \n\t"
      "movl %[sum], %[temp]   \n\t"
      "shrl $16, %[sum]       \n\t"
      "addw %w[temp], %w[sum] \n\t"
      "adcl $0, %[sum]        \n\t"
      "notl %[sum]            \n\t"
      : [sum] "+rm"(sum), [temp] "=&r"(temp)
      : "r"(buf32), [u321] "rm"(buf32[1]), [u322] "rm"(buf32[2]),
        [u323] "rm"(buf32[3]), [u324] "rm"(buf32[4]));

  return (static_cast<uint16_t>(sum) == 0x0000);
}

// Return IP checksum of the ip header 'iph' without ip options
// It skips the checksum field into the calculation
// It does not set the checksum field in ip header
static inline uint16_t CalculateIpv4NoOptChecksum(const Ipv4Header &iph) {
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&iph);
  uint32_t sum = buf32[0];
  uint32_t temp;

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  // 2. reduce to 16-bit unsigned integers
  // 3. negate
  asm(
      "addl %[u321], %[sum]    \n\t"
      "adcl %[u322], %[sum]    \n\t"
      "adcl %[u323], %[sum]    \n\t"
      "adcl %[u324], %[sum]    \n\t"
      "adcl $0, %[sum]         \n\t"
      "movl %[sum], %[temp]    \n\t"
      "shrl $16, %[sum]        \n\t"
      "addw %w[temp], %w[sum]  \n\t"
      "adcl $0, %[sum]         \n\t"
      "notl %[sum]             \n\t"
      : [sum] "+rm"(sum), [temp] "=&r"(temp)
      : "r"(buf32), [u321] "rm"(buf32[1]),
        [u322] "rm"(buf32[2] & 0xFFFF),  // skip checksum fields
        [u323] "rm"(buf32[3]), [u324] "rm"(buf32[4]));

  return static_cast<uint16_t>(sum);
}

// Return true if the TCP checksum is true with the TCP header and
// pseudo header info - source ip, destiniation ip, and tcp byte stream length
static inline bool VerifyIpv4TcpChecksum(
    const TcpHeader &tcph, uint32_t src_ip, uint32_t dst_ip,
    uint16_t tcp_len /* tcp header + data */) {
  static const uint32_t TCP = htons(0x06);  // TCP
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&tcph);
  uint32_t sum = buf32[0];
  uint32_t temp;

  // tcp options and data
  sum += CalculateSum(buf32 + 5, tcp_len - 20);

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  // 2. reduce to 16-bit unsigned integers
  // 3. negate
  asm(
      "addl %[u1], %[sum]      \n\t"
      "adcl %[u2], %[sum]      \n\t"
      "adcl %[u3], %[sum]      \n\t"
      "adcl %[u4], %[sum]      \n\t"
      "adcl %[src], %[sum]     \n\t"
      "adcl %[dst], %[sum]     \n\t"
      "adcl %[len], %[sum]     \n\t"
      "adcl %[tcp], %[sum]     \n\t"
      "adcl $0, %[sum]         \n\t"
      "movl %[sum], %[temp]    \n\t"
      "shrl $16, %[sum]        \n\t"
      "addw %w[temp], %w[sum]  \n\t"
      "adcl $0, %[sum]         \n\t"
      "notl %[sum]             \n\t"
      : [sum] "+rm"(sum), [temp] "=&r"(temp)
      : "r"(buf32), [u1] "rm"(buf32[1]), [u2] "rm"(buf32[2]),
        [u3] "rm"(buf32[3]), [u4] "rm"(buf32[4]), [src] "rm"(src_ip),
        [dst] "rm"(dst_ip), [len] "rm"(static_cast<uint32_t>(htons(tcp_len))),
        [tcp] "rm"(TCP));

  return (static_cast<uint16_t>(sum) == 0x0000);
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
  static const uint32_t TCP = htons(0x06);  // TCP
  const uint32_t *buf32 = reinterpret_cast<const uint32_t *>(&tcph);
  uint32_t sum = buf32[0];
  uint32_t temp;

  // tcp options and data
  sum += CalculateSum(buf32 + 5, tcp_len - 20);

  // Calculate internet checksum, the optimized way is
  // 1. get 32-bit one's complement sum including carrys
  // 2. reduce to 16-bit unsigned integers
  // 3. negate
  asm(
      "addl %[u1], %[sum]      \n\t"
      "adcl %[u2], %[sum]      \n\t"
      "adcl %[u3], %[sum]      \n\t"
      "adcl %[u4], %[sum]      \n\t"
      "adcl %[src], %[sum]     \n\t"
      "adcl %[dst], %[sum]     \n\t"
      "adcl %[len], %[sum]     \n\t"
      "adcl %[tcp], %[sum]     \n\t"
      "adcl $0, %[sum]         \n\t"
      "movl %[sum], %[temp]    \n\t"
      "shrl $16, %[sum]        \n\t"
      "addw %w[temp], %w[sum]  \n\t"
      "adcl $0, %[sum]         \n\t"
      "notl %[sum]             \n\t"
      : [sum] "+rm"(sum), [temp] "=&r"(temp)
      : "r"(buf32), [u1] "rm"(buf32[1]), [u2] "rm"(buf32[2]),
        [u3] "rm"(buf32[3]),
        [u4] "rm"(buf32[4] >> 16),  // skip checksum field
        [src] "rm"(src), [dst] "r"(dst),
        [len] "rm"(static_cast<uint32_t>(htons(tcp_len))), [tcp] "rm"(TCP));

  return static_cast<uint16_t>(sum);
}

// Return true if the TCP (on IPv4) checksum is true
static inline uint16_t CalculateIpv4TcpChecksum(const Ipv4Header &iph,
                                                const TcpHeader &tcph) {
  return CalculateIpv4TcpChecksum(tcph, iph.src, iph.dst,
                                  ntohs(iph.length) - (iph.header_length << 2));
}

// Return incrementally updated internet checksum of old_checksum
// when 'old_value' changes to 'new_value' e.g., changed IP address
static inline uint16_t CalculateChecksumIncrementalUpdate(uint16_t old_checksum,
                                                          uint32_t old_value,
                                                          uint32_t new_value) {
  // new checksum = ~(~old_checksum + ~old_value + new_value) by RFC 1624
  uint32_t sum = ~old_checksum & 0xFFFF;
  sum += (~old_value >> 16) + (~old_value & 0xFFFF);
  sum += (new_value >> 16) + (new_value & 0xFFFF);

  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  sum = ~sum;

  return static_cast<uint16_t>(sum);
}

// Return incrementally updated internet checksum of old_checksum
// when 'old_value' changes to 'new_value' e.g., changed port number
static inline uint16_t CalculateChecksumIncrementalUpdate(uint16_t old_checksum,
                                                          uint16_t old_value,
                                                          uint16_t new_value) {
  // new checksum = ~(~old_checksum + ~old_value + new_value) by RFC 1624
  uint32_t sum = ~old_checksum & 0xFFFF;
  sum += ~old_value & 0xFFFF;
  sum += new_value;

  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  sum = ~sum;

  return static_cast<uint16_t>(sum);
}

}  // namespace utils
}  // namespace bess

#endif
