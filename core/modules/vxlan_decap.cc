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

#include "vxlan_decap.h"

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/udp.h"
#include "../utils/vxlan.h"

/* TODO: Currently it decapulates the entire Ethernet/IP/UDP/VXLAN headers.
 *       Modularize. */

enum {
  ATTR_W_TUN_IP_SRC,
  ATTR_W_TUN_IP_DST,
  ATTR_W_TUN_ID,
};

CommandResponse VXLANDecap::Init(
    const bess::pb::VXLANDecapArg &arg[[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("tun_ip_src", 4, AccessMode::kWrite);
  AddMetadataAttr("tun_ip_dst", 4, AccessMode::kWrite);
  AddMetadataAttr("tun_id", 4, AccessMode::kWrite);

  return CommandSuccess();
}

void VXLANDecap::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::be32_t;
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;
  using bess::utils::Vxlan;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
    size_t ip_bytes = ip->header_length << 2;
    Udp *udp =
        reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);
    Vxlan *vh = reinterpret_cast<Vxlan *>(udp + 1);

    set_attr<be32_t>(this, ATTR_W_TUN_IP_SRC, pkt, ip->src);
    set_attr<be32_t>(this, ATTR_W_TUN_IP_DST, pkt, ip->dst);
    set_attr<be32_t>(this, ATTR_W_TUN_ID, pkt, vh->vx_vni >> 8);

    pkt->adj(sizeof(*eth) + ip_bytes + sizeof(*udp) + sizeof(*vh));
  }

  RunNextModule(batch);
}

ADD_MODULE(VXLANDecap, "vxlan_decap",
           "decapsulates the outer Ethetnet/IP/UDP/VXLAN headers")
