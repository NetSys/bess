#ifndef BESS_UTILS_IP_H_
#define BESS_UTILS_IP_H_

#include <string>

#include "endian.h"

namespace bess {
namespace utils {

// An IPv4 header definition loosely based on the BSD version.
struct[[gnu::packed]] Ipv4Header {
  enum Flag : uint16_t {
    kMF = 1 << 13,  // More fragments
    kDF = 1 << 14,  // Do not fragment
  };

#if __BYTE_ORDER == __LITTLE_ENDIAN
  uint8_t header_length : 4;  // Header length.
  uint8_t version : 4;        // Version.
#elif __BYTE_ORDER == __BIG_ENDIAN
  uint8_t version : 4;        // Version.
  uint8_t header_length : 4;  // Header length.
#else
#error __BYTE_ORDER must be defined.
#endif
  uint8_t type_of_service;  // Type of service.
  be16_t length;            // Length.
  be16_t id;                // ID.
  be16_t fragment_offset;   // Fragment offset.
  uint8_t ttl;              // Time to live.
  uint8_t protocol;         // Protocol.
  uint16_t checksum;        // Checksum.
  be32_t src;               // Source address.
  be32_t dst;               // Destination address.
};

struct CIDRNetwork {
  // Implicit default constructor is not allowed
  CIDRNetwork() = delete;

  // Construct CIDRNetwork from a string like "192.168.0.1/24"
  explicit CIDRNetwork(const std::string& cidr);

  // Returns true if ip is within the range of CIDRNetwork
  bool Match(const be32_t& ip) const { return (addr & mask) == (ip & mask); }

  be32_t addr;
  be32_t mask;
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_IP_ADDRESS_H_
