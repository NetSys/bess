#include "ip.h"

#include <glog/logging.h>

#include "format.h"

namespace bess {
namespace utils {

bool ParseIpv4Address(const std::string &str, be32_t *addr) {
  unsigned int a, b, c, d;
  int cnt;

  cnt = bess::utils::Parse(str, "%u.%u.%u.%u", &a, &b, &c, &d);
  if (cnt != 4 || a >= 256 || b >= 256 || c >= 256 || d >= 256) {
    return false;
  }

  *addr = be32_t((a << 24) | (b << 16) | (c << 8) | d);
  return true;
}

std::string ToIpv4Address(be32_t addr) {
  const union {
    be32_t addr;
    char bytes[4];
  } &t = {.addr = addr};

  return bess::utils::Format("%hhu.%hhu.%hhu.%hhu", t.bytes[0], t.bytes[1],
                             t.bytes[2], t.bytes[3]);
}

Ipv4Prefix::Ipv4Prefix(const std::string &prefix) {
  size_t delim_pos = prefix.find('/');

  // default values in case of parser failure
  addr = be32_t(0);
  mask = be32_t(0);

  if (prefix.length() == 0 || delim_pos == std::string::npos ||
      delim_pos >= prefix.length()) {
    return;
  }

  ParseIpv4Address(prefix.substr(0, delim_pos), &addr);

  const int len = std::stoi(prefix.substr(delim_pos + 1));
  if (len <= 0) {
    mask = be32_t(0);
  } else if (len >= 32) {
    mask = be32_t(0xffffffff);
  } else {
    mask = be32_t(~((1 << (32 - len)) - 1));
  }
}

}  // namespace utils
}  // namespace bess
