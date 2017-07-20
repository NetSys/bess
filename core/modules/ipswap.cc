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

#include "ipswap.h"

#include "utils/ether.h"
#include "utils/ip.h"
#include "utils/udp.h"

void IPSwap::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
    size_t ip_bytes = (ip->header_length & 0xf) << 2;
    Udp *udp =
        reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);

    // std::swap cannot be used for packed fields
    bess::utils::be32_t tmp_ip = ip->src;
    ip->src = ip->dst;
    ip->dst = tmp_ip;

    bess::utils::be16_t tmp_port;
    switch (ip->protocol) {
      case Ipv4::Proto::kTcp:
      case Ipv4::Proto::kUdp:
        // TCP and UDP share the same layout for ports
        tmp_port = udp->src_port;
        udp->src_port = udp->dst_port;
        udp->dst_port = tmp_port;
        break;
      case Ipv4::Proto::kIcmp:
        break;
      default:
        VLOG(1) << "Unknown protocol: " << ip->protocol;
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(IPSwap, "ipswap",
           "swaps source/destination IP addresses and L4 ports")
