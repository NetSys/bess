#ifndef BESS_UTILS_MPLS_H_
#define BESS_UTILS_MPLS_H_
#include <type_traits>
#include "rte_byteorder.h"

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

#define MPLS_LS_LABEL_MASK 0xFFFFF000
#define MPLS_LS_LABEL_SHIFT 12
#define MPLS_LS_TC_MASK 0x00000E00
#define MPLS_LS_TC_SHIFT 9
#define MPLS_LS_S_MASK 0x00000100
#define MPLS_LS_S_SHIFT 8
#define MPLS_LS_TTL_MASK 0x000000FF
#define MPLS_LS_TTL_SHIFT 0

struct[[gnu::packed]] Mpls {
  void setEntry(uint32_t label, uint8_t ttl, uint8_t tc, bool bos) {
    entry =
        be32_t((label << MPLS_LS_LABEL_SHIFT) | (tc << MPLS_LS_TC_SHIFT) |
               (bos ? (1 << MPLS_LS_S_SHIFT) : 0) | (ttl << MPLS_LS_TTL_SHIFT));
  }

  uint32_t getLabel() {
    be32_t mpls_entry = be32_t(entry);
    return (mpls_entry.value() & MPLS_LS_LABEL_MASK) >> MPLS_LS_LABEL_SHIFT;
  }

  uint8_t getTtl() {
    be32_t mpls_entry = be32_t(entry);
    return (mpls_entry.value() & MPLS_LS_TTL_MASK) >> MPLS_LS_TTL_SHIFT;
  }

  uint8_t getTc() {
    be32_t mpls_entry = be32_t(entry);
    return (mpls_entry.value() & MPLS_LS_TC_MASK) >> MPLS_LS_TC_SHIFT;
  }

  bool isBottomOfStack() {
    be32_t mpls_entry = be32_t(entry);
    return (mpls_entry.value() & MPLS_LS_S_MASK) >> MPLS_LS_S_SHIFT;
  }

  be32_t entry;
};

static_assert(std::is_pod<Mpls>::value, "not a POD type");
static_assert(sizeof(Mpls) == 4, "struct Mpls size is incorrect");

}  // namespace utils
}  // namespace bess

#endif /* BESS_UTILS_MPLS_H_ */
