#ifndef BESS_UTILS_IP_H_
#define BESS_UTILS_IP_H_

#include <string>

namespace bess {
namespace utils {

typedef uint32_t IPAddress;

struct CIDRNetwork {
  CIDRNetwork() = default;
  explicit CIDRNetwork(const std::string& cidr);

  bool Match(const IPAddress& ip) const { return (addr & mask) == (ip & mask); }

  IPAddress addr;
  IPAddress mask;
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_IP_ADDRESS_H_
