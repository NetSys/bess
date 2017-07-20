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

#include "ip_encap.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::be16_t;
using bess::utils::be32_t;

enum {
  ATTR_R_IP_SRC,
  ATTR_R_IP_DST,
  ATTR_R_IP_PROTO,
  ATTR_W_IP_NEXTHOP,
  ATTR_W_ETHER_TYPE,
};

CommandResponse IPEncap::Init(const bess::pb::IPEncapArg &arg[[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("ip_src", 4, AccessMode::kRead);
  AddMetadataAttr("ip_dst", 4, AccessMode::kRead);
  AddMetadataAttr("ip_proto", 1, AccessMode::kRead);
  AddMetadataAttr("ip_nexthop", 4, AccessMode::kWrite);
  AddMetadataAttr("ether_type", 2, AccessMode::kWrite);

  return CommandSuccess();
}

void IPEncap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    be32_t ip_src = get_attr<be32_t>(this, ATTR_R_IP_SRC, pkt);
    be32_t ip_dst = get_attr<be32_t>(this, ATTR_R_IP_DST, pkt);
    uint8_t ip_proto = get_attr<uint8_t>(this, ATTR_R_IP_PROTO, pkt);

    Ipv4 *iph;

    uint16_t total_len = pkt->total_len() + sizeof(*iph);

    iph = static_cast<Ipv4 *>(pkt->prepend(sizeof(*iph)));

    if (unlikely(!iph)) {
      continue;
    }

    iph->version = 0x4;
    iph->header_length = sizeof(*iph) / 4;
    iph->type_of_service = 0;
    iph->length = be16_t(total_len);
    iph->fragment_offset = be16_t(Ipv4::Flag::kDF);
    iph->ttl = 64;
    iph->protocol = ip_proto;
    iph->src = ip_src;
    iph->dst = ip_dst;

    iph->checksum = bess::utils::CalculateIpv4NoOptChecksum(*iph);

    set_attr<be32_t>(this, ATTR_W_IP_NEXTHOP, pkt, ip_dst);
    set_attr<be16_t>(this, ATTR_W_ETHER_TYPE, pkt,
                     be16_t(Ethernet::Type::kIpv4));
  }

  RunNextModule(batch);
}

ADD_MODULE(IPEncap, "ip_encap", "encapsulates packets with an IPv4 header")
