#ifndef BESS_UTILS_ICMP_H_

namespace bess {
namespace utils {

// A basic ICMP header definition.
struct[[gnu::packed]] Icmp {
  uint8_t type;       // ICMP packet type.
  uint8_t code;       // ICMP packet code.
  uint16_t checksum;  // ICMP packet checksum.
  be16_t ident;       // ICMP packet identifier.
  be16_t seq_num;     // ICMP packet sequence number
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ICMP_H_
