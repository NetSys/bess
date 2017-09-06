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

#include "mpls_pop.h"

#include "../utils/ether.h"
#include "../utils/mpls.h"

using bess::utils::Ethernet;
using bess::utils::Mpls;

const Commands MPLSPop::cmds = {{"set", "MplsPopArg",
                                 MODULE_CMD_FUNC(&MPLSPop::CommandSet),
                                 Command::THREAD_UNSAFE}};

// TODO(gsagie) make the next eth type and remove eth header
//              configurable per MPLS label (with default)
MPLSPop::MPLSPop()
    : next_ether_type_(be16_t(Ethernet::Type::kIpv4)),
      remove_eth_header_(false) {
  max_allowed_workers_ = Worker::kMaxWorkers;
}

void MPLSPop::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();

    if (eth->ether_type != be16_t(Ethernet::Type::kMpls)) {
      // non MPLS packets are sent to different output gate
      out_gates[i] = 1;
      continue;
    }
    out_gates[i] = 0;

    // TODO(gsagie) save the MPLS label as metadata
    // Mpls *mpls = reinterpret_cast<Mpls *>(eth_header + 1);

    // TODO(gsagie) convert this to be more efficient using Intel instructions
    if (remove_eth_header_) {
      pkt->adj(sizeof(Ethernet) + sizeof(Mpls));
    } else {
      Ethernet::Address src_addr = eth->src_addr;
      Ethernet::Address dst_addr = eth->dst_addr;

      pkt->adj(sizeof(Mpls));
      Ethernet *eth_new = pkt->head_data<Ethernet *>();
      eth_new->src_addr = src_addr;
      eth_new->dst_addr = dst_addr;
      eth_new->ether_type = next_ether_type_;
    }
  }

  RunSplit(out_gates, batch);
}

CommandResponse MPLSPop::CommandSet(const bess::pb::MplsPopArg &arg) {
  remove_eth_header_ = arg.remove_eth_header();
  next_ether_type_ = be16_t(arg.next_eth_type());
  return CommandSuccess();
}

ADD_MODULE(MPLSPop, "mpls_pop", "Pop MPLS label")
