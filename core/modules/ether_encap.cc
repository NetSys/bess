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

#include "ether_encap.h"

#include "../utils/ether.h"

using bess::utils::Ethernet;

enum {
  ATTR_R_ETHER_SRC,
  ATTR_R_ETHER_DST,
  ATTR_R_ETHER_TYPE,
};

CommandResponse EtherEncap::Init(
    const bess::pb::EtherEncapArg &arg[[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("ether_src", sizeof(Ethernet::Address), AccessMode::kRead);
  AddMetadataAttr("ether_dst", sizeof(Ethernet::Address), AccessMode::kRead);
  AddMetadataAttr("ether_type", 2, AccessMode::kRead);

  return CommandSuccess();
};

void EtherEncap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet::Address ether_src;
    Ethernet::Address ether_dst;
    bess::utils::be16_t ether_type;

    ether_src = get_attr<Ethernet::Address>(this, ATTR_R_ETHER_SRC, pkt);
    ether_dst = get_attr<Ethernet::Address>(this, ATTR_R_ETHER_DST, pkt);
    ether_type = get_attr<bess::utils::be16_t>(this, ATTR_R_ETHER_TYPE, pkt);

    Ethernet *eth = static_cast<Ethernet *>(pkt->prepend(sizeof(*eth)));

    // not enough headroom?
    if (unlikely(!eth)) {
      continue;
    }

    eth->dst_addr = ether_dst;
    eth->src_addr = ether_src;
    eth->ether_type = ether_type;
  }

  RunNextModule(batch);
}

ADD_MODULE(EtherEncap, "ether_encap",
           "encapsulates packets with an Ethernet header")
