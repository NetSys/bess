#ifndef BESS_UTILS_ETHER_H_
#define BESS_UTILS_ETHER_H_

#include <cstdint>
#include <string>
#include <type_traits>

#include "endian.h"

namespace bess {
namespace utils {

struct[[gnu::packed]] EthHeader {
  struct[[gnu::packed]] Address {
    Address() = default;
    Address(const std::string &str);

    static const size_t kSize = 6;

    // Parses str in "aA:Bb:00:11:22:33" format and saves the address in parsed
    // Returns false if the format is incorrect.
    //   (in that case, the content of parsed is undefined.)
    bool FromString(const std::string &str);

    // Returns "aa:bb:00:11:22:33" (all in lower case)
    std::string ToString() const;

    void Randomize();

    bool operator==(const Address &o) const {
      for (size_t i = 0; i < kSize; i++) {
        if (bytes[i] != o.bytes[i]) {
          return false;
        }
      }
      return true;
    }

    bool operator!=(const Address &o) const {
      for (size_t i = 0; i < kSize; i++) {
        if (bytes[i] != o.bytes[i]) {
          return true;
        }
      }
      return false;
    }

    char bytes[kSize];
  };

  enum Type : uint16_t {
    kIpv4 = 0x0800,
    kArp = 0x0806,
    kVlan = 0x8100,
    kQinQ = 0x88a8,  // 802.1ad double-tagged VLAN packets
    kIpv6 = 0x86DD,
  };

  Address dst_addr;
  Address src_addr;
  be16_t ether_type;
};

static_assert(std::is_pod<EthHeader>::value, "not a POD type");
static_assert(std::is_pod<EthHeader::Address>::value, "not a POD type");
static_assert(sizeof(EthHeader) == 14, "struct EthHeader is incorrect");

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ETHER_H_
