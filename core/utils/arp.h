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

#ifndef BESS_UTILS_ARP_H_
#define BESS_UTILS_ARP_H_

#include "endian.h"
#include "ether.h"
#include "ip.h"

namespace bess {
namespace utils {

// A basic ARP header definition
struct[[gnu::packed]] Arp {
  // Ethernet hardware format for hrd
  enum HardwareAddress : uint16_t {
    kEthernet = 1,
  };

  enum Opcode : uint16_t {
    kRequest = 1,
    kReply = 2,
    kRevRequest = 3,
    kRevReply = 4,
    kInvRequest = 8,
    kInvReply = 9,
  };

  be16_t hw_addr;            // format of hardware address (hrd)
  be16_t proto_addr;         // format of protocol address (pro)
  uint8_t hw_addr_length;    // length of hardware address (hln)
  uint8_t proto_addr_length; // length of protocol address (pln)
  be16_t opcode;             // ARP opcode (command) (op)

  // ARP Data
  Ethernet::Address sender_hw_addr;  // sender hardware address (sha)
  be32_t sender_ip_addr;             // sender IP address (sip)
  Ethernet::Address target_hw_addr;  // target hardware address (tha)
  be32_t target_ip_addr;            // target IP address (tip)
};

  static_assert(sizeof(Arp) == 28, "struct Arp size is incorrect");

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ARP_H_

