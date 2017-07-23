// Copyright (c) 2017, Cloudigo.
// Copyright (c) 2017, Nefeli Networks, Inc.
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

#ifndef BESS_MODULES_ARP_RESPONDER_H_
#define BESS_MODULES_ARP_RESPONDER_H_

#include <map>

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../utils/endian.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

using bess::utils::Ethernet;
using bess::utils::be32_t;

// ARP cache entry struct which keeps mapping between IP and MAC
struct arp_entry {
  Ethernet::Address mac_addr;
  be32_t ip_addr;
  uint64_t time;  // timestamp used to expire cache entries (in milliseconds)
};

// ARP Responder module
// Answer ARP requests from an internal configurable cache
// Currently drops non ARP packets
class ArpResponder final : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands cmds;

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::ArpResponderArg &arg);

 private:
  // Mapping between IP (key) and its ARP entry (MAC Address)
  std::map<be32_t, struct arp_entry> entries_;
};

#endif  // BESS_MODULES_ARP_RESPONDER_H_
