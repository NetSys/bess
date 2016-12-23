#include "ip.h"

#include <arpa/inet.h>
#include <glog/logging.h>
#include <sys/socket.h>

namespace bess {
namespace utils {

CIDRNetwork::CIDRNetwork(const std::string &cidr) {
  if (cidr.length() == 0) {
    addr = 0;
    mask = 0;
    return;
  }

  size_t delim_pos = cidr.find('/');
  DCHECK_NE(delim_pos, std::string::npos);
  DCHECK_LT(delim_pos, cidr.length());

  inet_pton(AF_INET, cidr.substr(0, delim_pos).c_str(), &addr);

  const int len = std::stoi(cidr.substr(delim_pos + 1));
  if (len <= 0) {
    mask = 0;
  } else if (len >= 32) {
    mask = 0xffffffff;
  } else {
    mask = ~((1 << (32 - len)) - 1);
  }
  mask = htonl(mask);
}

}  // namespace utils
}  // namespace bess
