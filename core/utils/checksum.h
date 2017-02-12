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

static inline uint32_t __attribute__((always_inline))
CalculateSum(const void *buf, size_t len, uint16_t sum16 = 0) {
  const uint64_t *u64 = reinterpret_cast<const uint64_t *>(buf);
  uint64_t sum64 = sum16;

#if __AVX2__
  if (len >= sizeof(uint64_t) * 12) {
    union {
      __m256i sum256;
      uint64_t sumb64[4];
      uint32_t sumb32[8];
    };
    sum256 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(u64));
    u64 += 4;
    len -= sizeof(uint64_t) * 4;

    union {
      __m256i overflow256;
      uint64_t ofb[4];
    };
    overflow256 = _mm256_setzero_si256();

    while (len >= sizeof(uint64_t) * 4) {
      __m256i temp256 =
          _mm256_loadu_si256(reinterpret_cast<const __m256i *>(u64));
      sum256 = _mm256_add_epi64(sum256, temp256);

      // handling carry flags:
      // use 'mask64' for conducting unsigned comparison
      const __m256i mask64 = _mm256_set1_epi64x(0x8000000000000000L);
      overflow256 = _mm256_sub_epi64(
          overflow256,
          // results of temp256 >= sum256, meaning overflow
          _mm256_cmpgt_epi64(_mm256_xor_si256(temp256, mask64),
                             _mm256_xor_si256(sum256, mask64)));
      len -= sizeof(uint64_t) * 4;
      u64 += 4;
    }

    // transfer data from simd register to normal
    // c++ compiler does faster than inline asm
    // the sum of overflow should not raise overflow
    sum64 += ofb[0] + ofb[1] + ofb[2] + ofb[3];
    sum64 +=
        static_cast<uint64_t>(sumb32[0]) + static_cast<uint64_t>(sumb32[1]) +
        static_cast<uint64_t>(sumb32[2]) + static_cast<uint64_t>(sumb32[3]) +
        static_cast<uint64_t>(sumb32[4]) + static_cast<uint64_t>(sumb32[5]) +
        static_cast<uint64_t>(sumb32[6]) + static_cast<uint64_t>(sumb32[7]);
  }
#else
  if (len >= sizeof(uint64_t) * 8) {
    while (len >= sizeof(uint64_t) * 8) {
      asm("addq %[u0], %[sum]	\n\t"
          "adcq %[u1], %[sum]	\n\t"
          "adcq %[u2], %[sum]	\n\t"
          "adcq %[u3], %[sum]	\n\t"
          "adcq %[u4], %[sum]	\n\t"
          "adcq %[u5], %[sum]	\n\t"
          "adcq %[u6], %[sum]	\n\t"
          "adcq %[u7], %[sum]	\n\t"
          "adcq $0, %[sum]"
          : [sum] "+rm"(sum64)
          : "r"(u64), [u0] "rm"(u64[0]), [u1] "rm"(u64[1]), [u2] "rm"(u64[2]),
            [u3] "rm"(u64[3]), [u4] "rm"(u64[4]), [u5] "rm"(u64[5]),
            [u6] "rm"(u64[6]), [u7] "rm"(u64[7]));
      len -= sizeof(uint64_t) * 8;
      u64 += 8;
    }
  }
#endif

  while (len >= sizeof(uint64_t) * 2) {
    asm("addq %[u0], %[sum]	\n\t"
        "adcq %[u1], %[sum]	\n\t"
        "adcq $0, %[sum]"
        : [sum] "+rm"(sum64)
        : "r"(u64), [u0] "rm"(u64[0]), [u1] "rm"(u64[1]));
    len -= sizeof(uint64_t) * 2;
    u64 += 2;
  }

  const uint16_t *u16 = reinterpret_cast<const uint16_t *>(u64);
  while (len >= sizeof(uint16_t)) {
    sum64 += *u16++;
    len -= sizeof(uint16_t);
  }

  if (len == 1) {
    sum64 += *reinterpret_cast<const uint8_t *>(u16);
  }

  sum64 = (sum64 >> 32) + (sum64 & 0xFFFFFFFF);
  sum64 += (sum64 >> 32);

  return static_cast<uint32_t>(sum64);
}

static inline uint16_t CalculateGenericChecksum(const void *buf, size_t len,
                                                uint16_t sum16 = 0) {
  uint32_t sum = CalculateSum(buf, len, sum16);

  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  sum = ~sum;

  return static_cast<uint16_t>(sum);
}

static inline bool VerifyGenericChecksum(const void *ptr, size_t len,
                                         uint16_t cksum) {
  uint16_t ret = CalculateGenericChecksum(ptr, len, cksum);
  return (ret == 0x0000);
}

static inline bool VerifyGenericChecksum(const void *buf, size_t len) {
  return VerifyGenericChecksum(buf, len, 0);
}

static inline bool VerifyIpv4NoOptChecksum(const Ipv4Header &iph) {
  const uint32_t *u32 = reinterpret_cast<const uint32_t *>(&iph);
  uint32_t sum = u32[0];
  uint32_t temp = 0;

  asm("addl %[u321], %[sum]	\n\t"
      "adcl %[u322], %[sum]	\n\t"
      "adcl %[u323], %[sum]	\n\t"
      "adcl %[u324], %[sum]	\n\t"
      "adcl $0, %[sum]			\n\t"
      "movl	%[sum], %[temp]	\n\t"
      "shrl $16, %[sum]			\n\t"
      "addw %w[temp], %w[sum]	\n\t"
      "adcl $0, %[sum]			\n\t"
      "notl %[sum]					\n\t"
      : [sum] "+rm"(sum), [temp] "=&r"(temp)
      : "r"(u32), [u321] "rm"(u32[1]), [u322] "rm"(u32[2]), [u323] "rm"(u32[3]),
        [u324] "rm"(u32[4]));

  return (static_cast<uint16_t>(sum) == 0x0000);
}

static inline uint16_t CalculateIpv4NoOptChecksum(const Ipv4Header &iph) {
  const uint32_t *u32 = reinterpret_cast<const uint32_t *>(&iph);
  uint32_t sum = u32[0];
  uint32_t temp;

  asm("addl %[u321], %[sum]	\n\t"
      "adcl %[u322], %[sum]	\n\t"
      "adcl %[u323], %[sum]	\n\t"
      "adcl %[u324], %[sum]	\n\t"
      "adcl $0, %[sum]			\n\t"
      "movl	%[sum], %[temp]	\n\t"
      "shrl $16, %[sum]			\n\t"
      "addw %w[temp], %w[sum]	\n\t"
      "adcl $0, %[sum]			\n\t"
      "notl %[sum]					\n\t"
      : [sum] "+rm"(sum), [temp] "=&r"(temp)
      : "r"(u32), [u321] "rm"(u32[1]),
        [u322] "rm"(u32[2] & 0xFFFF),  // skip checksum fields
        [u323] "rm"(u32[3]), [u324] "rm"(u32[4]));

  return static_cast<uint16_t>(sum);
}

static inline bool VerifyIpv4TcpChecksum(
    const TcpHeader &tcph, uint32_t src, uint32_t dst,
    uint16_t tcp_len /* tcp header + data */) {
  static const uint32_t protocol = htons(0x06);  // TCP
  const uint32_t *u32 = reinterpret_cast<const uint32_t *>(&tcph);
  uint32_t sum = u32[0];
  uint32_t temp;

  // tcp options and data
  sum += CalculateSum(u32 + 5, tcp_len - 20);

  asm("addl %[u1], %[sum]	\n\t"
      "adcl %[u2], %[sum]	\n\t"
      "adcl %[u3], %[sum]	\n\t"
      "adcl %[u4], %[sum]	\n\t"
      "adcl %[src], %[sum]	\n\t"
      "adcl %[dst], %[sum]	\n\t"
      "adcl %[len], %[sum]	\n\t"
      "adcl %[protocol], %[sum]	\n\t"
      "adcl $0, %[sum]			\n\t"
      "movl	%[sum], %[temp]	\n\t"
      "shrl $16, %[sum]			\n\t"
      "addw %w[temp], %w[sum]	\n\t"
      "adcl $0, %[sum]			\n\t"
      "notl %[sum]					\n\t"
      : [sum] "+rm"(sum), [temp] "=&r"(temp)
      : "r"(u32), [u1] "rm"(u32[1]), [u2] "rm"(u32[2]), [u3] "rm"(u32[3]),
        [u4] "rm"(u32[4]), [src] "rm"(src), [dst] "rm"(dst),
        [len] "rm"(static_cast<uint32_t>(htons(tcp_len))),
        [protocol] "rm"(protocol));

  return (static_cast<uint16_t>(sum) == 0x0000);
}

static inline bool VerifyIpv4TcpChecksum(const Ipv4Header &iph,
                                         const TcpHeader &tcph) {
  return VerifyIpv4TcpChecksum(tcph, iph.src, iph.dst,
                               ntohs(iph.length) - (iph.header_length << 2));
}

static inline uint16_t CalculateIpv4TcpChecksum(
    const TcpHeader &tcph, uint32_t src, uint32_t dst,
    uint16_t tcp_len /* host-order: tcp header + data */) {
  static const uint32_t protocol = htons(0x06);  // TCP
  const uint32_t *u32 = reinterpret_cast<const uint32_t *>(&tcph);
  uint32_t sum = u32[0];
  uint32_t temp;

  // tcp options and data
  sum += CalculateSum(u32 + 5, tcp_len - 20);

  asm("addl %[u1], %[sum]	\n\t"
      "adcl %[u2], %[sum]	\n\t"
      "adcl %[u3], %[sum]	\n\t"
      "adcl %[u4], %[sum]	\n\t"
      "adcl %[src], %[sum]	\n\t"
      "adcl %[dst], %[sum]	\n\t"
      "adcl %[len], %[sum]	\n\t"
      "adcl %[protocol], %[sum]	\n\t"
      "adcl $0, %[sum]			\n\t"
      "movl	%[sum], %[temp]	\n\t"
      "shrl $16, %[sum]			\n\t"
      "addw %w[temp], %w[sum]	\n\t"
      "adcl $0, %[sum]			\n\t"
      "notl %[sum]					\n\t"
      : [sum] "+rm"(sum), [temp] "=&r"(temp)
      : "r"(u32), [u1] "rm"(u32[1]), [u2] "rm"(u32[2]), [u3] "rm"(u32[3]),
        [u4] "rm"(u32[4] >> 16),  // skip checksum field
        [src] "rm"(src), [dst] "r"(dst),
        [len] "rm"(static_cast<uint32_t>(htons(tcp_len))),
        [protocol] "rm"(protocol));

  return static_cast<uint16_t>(sum);
}

static inline uint16_t CalculateIpv4TcpChecksum(const Ipv4Header &iph,
                                                const TcpHeader &tcph) {
  return CalculateIpv4TcpChecksum(tcph, iph.src, iph.dst,
                                  ntohs(iph.length) - (iph.header_length << 2));
}

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
