// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// Copyright (c) 2017, Cloudigo.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_UTILS_ETHER_H_
#define BESS_UTILS_ETHER_H_

#include <cstdint>
#include <string>
#include <type_traits>

#include "endian.h"

namespace bess {
namespace utils {

struct[[gnu::packed]] Ethernet {
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
    kMpls = 0x8847,
  };

  Address dst_addr;
  Address src_addr;
  be16_t ether_type;
};

struct[[gnu::packed]] Vlan {
  be16_t tci;
  be16_t ether_type;
};

static_assert(std::is_pod<Ethernet>::value, "not a POD type");
static_assert(std::is_pod<Ethernet::Address>::value, "not a POD type");
static_assert(sizeof(Ethernet) == 14, "struct Ethernet is incorrect");
static_assert(std::is_pod<Vlan>::value, "not a POD type");
static_assert(sizeof(Vlan) == 4, "struct Vlan is incorrectly sized");

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ETHER_H_
