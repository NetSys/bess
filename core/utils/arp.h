#ifndef BESS_UTILS_ARP_H_
#define BESS_UTILS_ARP_H_

#include "endian.h"
#include "ether.h"
#include "ip.h"

namespace bess {
namespace utils {

// A basic ARP header definition.
struct[[gnu::packed]] ARP {
  // Ethernet hardware format for hrd
  static const be16_t kEtherHardwareFormat = 1;

  enum Opcode : uint16_t {
    REQUEST = 1,
    REPLY = 2,
    REVREQUEST = 3,
    REVREPLY = 4,
    INVREQUEST = 8,
    INVREPLY = 8,
  };

  be16_t hrd;   // format of hardware address
  be16_t pro;   // format of protocol address
  uint8_t hln;  // length of hardware address
  uint8_t pln;  // length of protocol address
  be16_t op;    // ARP opcode (command)

  // ARP Data
  Ethernet::Address sha;  // sender hardware address
  Ipv4 sip;               // sender IP address
  Ethernet::Address tha;  // target hardware address
  Ipv4 tip;               // target IP address
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ARP_H_

