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

#include "arp_responder.h"

#include "../utils/arp.h"

using bess::utils::Arp;
using bess::utils::be16_t;

const Commands ArpResponder::cmds = {
    {"add", "ArpResponderArg", MODULE_CMD_FUNC(&ArpResponder::CommandAdd),
     Command::THREAD_UNSAFE}};

CommandResponse ArpResponder::CommandAdd(const bess::pb::ArpResponderArg &arg) {
  be32_t ip_addr;
  arp_entry entry;

  if (!arg.ip().length()) {
    return CommandFailure(EINVAL, "IP address is missing");
  }
  if (!bess::utils::ParseIpv4Address(arg.ip(), &ip_addr)) {
    return CommandFailure(EINVAL, "Invalid IP Address: %s", arg.ip().c_str());
  }

  if (!entry.mac_addr.FromString(arg.mac_addr())) {
    return CommandFailure(EINVAL, "Invalid MAC Address: %s",
                          arg.mac_addr().c_str());
  }

  entry.ip_addr = ip_addr;
  entries_[ip_addr] = entry;
  return CommandSuccess();
}

void ArpResponder::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    out_gates[i] = 0;

    Ethernet *eth = pkt->head_data<Ethernet *>();
    if (eth->ether_type != be16_t(Ethernet::Type::kArp)) {
      // Currently drop all non ARP packets, but can also just continue
      out_gates[i] = DROP_GATE;
      continue;
    }

    Arp *arp = reinterpret_cast<Arp *>(eth + 1);
    if (arp->opcode == be16_t(Arp::Opcode::kRequest)) {
      // TODO(galsagie) When learn is added, learn SRC MAC here

      // Try to find target IP in cache, if exists convert request to reply
      auto it = entries_.find(arp->target_ip_addr);
      if (it != entries_.end()) {
        const struct arp_entry &entry = it->second;
        arp->opcode = be16_t(Arp::Opcode::kReply);

        eth->dst_addr = eth->src_addr;
        eth->src_addr = entry.mac_addr;

        arp->target_hw_addr = arp->sender_hw_addr;
        arp->sender_hw_addr = entry.mac_addr;

        arp->target_ip_addr = arp->sender_ip_addr;
        arp->sender_ip_addr = entry.ip_addr;
      } else {
        // Did not find an ARP entry in cache, drop packet
        // TODO(galsagie) Optinally continue packet to next module here
        out_gates[i] = DROP_GATE;
      }
    } else if (arp->opcode == be16_t(Arp::Opcode::kReply)) {
      // TODO(galsagie) When learn is added, learn SRC MAC here
      out_gates[i] = DROP_GATE;
    } else {
      // TODO(galsagie) Other opcodes are not handled yet.
      out_gates[i] = DROP_GATE;
    }
  }

  RunSplit(out_gates, batch);
}

ADD_MODULE(ArpResponder, "arp_responder",
           "Respond to ARP requests and learns new MAC's")
