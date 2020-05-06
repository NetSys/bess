// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
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

#include "ip_checksum.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

enum { FORWARD_GATE = 0, FAIL_GATE };

void IPChecksum::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  using bess::utils::be16_t;
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Vlan;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    Ethernet *eth = batch->pkts()[i]->head_data<Ethernet *>();
    void *data = eth + 1;
    Ipv4 *ip;

    be16_t ether_type = eth->ether_type;

    if (ether_type == be16_t(Ethernet::Type::kQinQ)) {
      Vlan *qinq = reinterpret_cast<Vlan *>(data);
      data = qinq + 1;
      ether_type = qinq->ether_type;
      if (ether_type != be16_t(Ethernet::Type::kVlan)) {
	EmitPacket(ctx, batch->pkts()[i], FORWARD_GATE);
	continue;
      }
    }

    if (ether_type == be16_t(Ethernet::Type::kVlan)) {
      Vlan *vlan = reinterpret_cast<Vlan *>(data);
      data = vlan + 1;
      ether_type = vlan->ether_type;
    }

    if (ether_type == be16_t(Ethernet::Type::kIpv4)) {
      ip = reinterpret_cast<Ipv4 *>(data);
    } else {
      EmitPacket(ctx, batch->pkts()[i], FORWARD_GATE);
      continue;
    }

    if (verify_) {
      EmitPacket(ctx, batch->pkts()[i], (VerifyIpv4Checksum(*ip)) ? FORWARD_GATE : FAIL_GATE);
    } else {
      ip->checksum = CalculateIpv4Checksum(*ip);
      EmitPacket(ctx, batch->pkts()[i], FORWARD_GATE);
    }
  }
}

CommandResponse IPChecksum::Init(const bess::pb::IPChecksumArg &arg) {
  verify_ = arg.verify();
  return CommandSuccess();
}

ADD_MODULE(IPChecksum, "ip_checksum", "recomputes the IPv4 checksum")
