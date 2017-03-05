#ifndef BESS_UTILS_IP_H_
#define BESS_UTILS_IP_H_

#include <arpa/inet.h>
#include <string>

namespace bess {
namespace utils {

typedef uint32_t IPAddress;

struct CIDRNetwork {
  // Implicit default constructor is not allowed
  CIDRNetwork() = delete;

  // Construct CIDRNetwork from a string like "192.168.0.1/24"
  explicit CIDRNetwork(const std::string& cidr);

  // Returns true if ip is within the range of CIDRNetwork
  bool Match(const IPAddress& ip) const { return (addr & mask) == (ip & mask); }

  IPAddress addr;
  IPAddress mask;
};

// An IPv4 header definition loosely based on the BSD version.
struct[[gnu::packed]] Ipv4Header {
  enum Flag : uint16_t {
    kMF = 1 << 13,  // More fragments
    kDF = 1 << 14,  // Do not fragment
  };

#if __BYTE_ORDER == __LITTLE_ENDIAN
  unsigned int header_length : 4;  // Header length.
  unsigned int version : 4;        // Version.
#elif __BYTE_ORDER == __BIG_ENDIAN
  unsigned int version : 4;        // Version.
  unsigned int header_length : 4;  // Header length.
#else
#error __BYTE_ORDER must be defined.
#endif
  uint8_t type_of_service;   // Type of service.
  uint16_t length;           // Length.
  uint16_t id;               // ID.
  uint16_t fragment_offset;  // Fragment offset.
  uint8_t ttl;               // Time to live.
  uint8_t protocol;          // Protocol.
  uint16_t checksum;         // Checksum.
  uint32_t src;              // Source address.
  uint32_t dst;              // Destination address.
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_IP_ADDRESS_H_
