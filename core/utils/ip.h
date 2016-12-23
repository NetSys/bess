#ifndef BESS_UTILS_IP_H_
#define BESS_UTILS_IP_H_

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

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_IP_ADDRESS_H_
