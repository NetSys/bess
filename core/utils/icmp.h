#ifndef BESS_UTILS_ICMP_H_

namespace bess {
namespace utils {

// A basic UDP header definition loosely based on the BSD version.
struct[[gnu::packed]] IcmpHeader {
  uint16_t type;      // ICMP packet type.
  uint16_t code;      // ICMP packet type.
  uint16_t checksum;  // ICMP packet checksum.
  uint16_t ident;     // ICMP packet identifier.
  uint16_t seq_num;   // ICMP packet sequence number
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ICMP_H_
