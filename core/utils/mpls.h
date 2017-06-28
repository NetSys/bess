#ifndef BESS_UTILS_MPLS_H_
#define BESS_UTILS_MPLS_H_

#include "rte_byteorder.h"

namespace bess {
namespace utils {

/* MPLS header definition, Reference: RFC 5462, RFC 3032
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                Label                  | TC  |S|       TTL     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	Label:  Label Value, 20 bits
 *	TC:     Traffic Class field, 3 bits
 *	S:      Bottom of Stack, 1 bit
 *	TTL:    Time to Live, 8 bits
 */
struct[[gnu::packed]] Mpls {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  uint32_t ttl : 8;     // Time to Live, 8 bits
  uint32_t s : 1;       // Bottom of Stack, 1 bit
  uint32_t tc : 3;      // Traffic Class field, 3 bits
  uint32_t label : 20;  // Label Value, 20 bits
#elif  __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  uint32_t label : 20;  // Label Value, 20 bits
  uint32_t tc : 3;      // Traffic Class field, 3 bits
  uint32_t s : 1;       // Bottom of Stack, 1 bit
  uint32_t ttl : 8;     // Time to Live, 8 bits
#endif
};

static_assert(sizeof(Mpls) == 4, "struct Mpls size is incorrect");

}  // namespace utils
}  // namespace bess

#endif /* BESS_UTILS_MPLS_H_ */
