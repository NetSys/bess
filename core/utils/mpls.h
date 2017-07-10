#ifndef BESS_UTILS_MPLS_H_
#define BESS_UTILS_MPLS_H_
#include "rte_byteorder.h"
#include <type_traits>

namespace bess {
namespace utils {

// MPLS header definition, Reference: RFC 5462, RFC 3032
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                Label                  | TC  |S|       TTL     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//	Label:  Label Value, 20 bits
//	TC:     Traffic Class field, 3 bits
//	S:      Bottom of Stack, 1 bit
//	TTL:    Time to Live, 8 bits

struct Mpls {
  static const uint32_t kMplsLabelMask = 0xFFFFF000;
  static const uint32_t kMplsLabelShift = 12;
  static const uint32_t kMplsTcMask = 0x00000E00;
  static const uint32_t kMplsTcShift = 9;
  static const uint32_t kMplsBosMask = 0x00000100;
  static const uint32_t kMplsBosShift = 8;
  static const uint32_t kMplsTtlMask = 0x000000FF;
  static const uint32_t kMplsTtlShift = 0;

  void setEntry(uint32_t label, uint8_t ttl, uint8_t tc, bool bos) {
    tag = be32_t((label << kMplsLabelShift) | (tc << kMplsTcShift) |
                 (bos ? (1 << kMplsBosShift) : 0) | (ttl << kMplsTtlShift));
  }

  uint32_t getLabel() {
    be32_t mpls_entry = be32_t(tag);
    return (mpls_entry.value() & kMplsLabelMask) >> kMplsLabelShift;
  }

  uint8_t getTtl() {
    be32_t mpls_entry = be32_t(tag);
    return (mpls_entry.value() & kMplsTtlMask) >> kMplsTtlShift;
  }

  uint8_t getTc() {
    be32_t mpls_entry = be32_t(tag);
    return (mpls_entry.value() & kMplsTcMask) >> kMplsTcShift;
  }

  bool isBottomOfStack() {
    be32_t mpls_entry = be32_t(tag);
    return (mpls_entry.value() & kMplsBosMask) >> kMplsBosShift;
  }

  union {
    struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      uint32_t ttl : 8;     // Time to Live, 8 bits
      uint32_t s : 1;       // Bottom of Stack, 1 bit
      uint32_t tc : 3;      // Traffic Class field, 3 bits
      uint32_t label : 20;  // Label Value, 20 bits
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      uint32_t label : 20;  // Label Value, 20 bits
      uint32_t tc : 3;      // Traffic Class field, 3 bits
      uint32_t s : 1;       // Bottom of Stack, 1 bit
      uint32_t ttl : 8;     // Time to Live, 8 bits
#endif
    } entry;

    be32_t tag;
  };
};

static_assert(sizeof(Mpls) == 4, "struct Mpls size is incorrect");

}  // namespace utils
}  // namespace bess

#endif /* BESS_UTILS_MPLS_H_ */
