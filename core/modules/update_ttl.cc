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

#include "update_ttl.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;

void UpdateTTL::ProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch out_batch;
  bess::PacketBatch drop_batch;
  out_batch.clear();
  drop_batch.clear();

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

    if (ip->ttl > 1) {
      // N to N-1 and 2 to 1 are identical for checksum purpose
      // We use constant numbers here for efficiency.
      ip->checksum = bess::utils::UpdateChecksum16(ip->checksum, 2, 1);
      ip->ttl -= 1;
      out_batch.add(pkt);
    } else {
      drop_batch.add(pkt);  // drop the packet since it's TTL is 1 or 0
    }
  }

  RunChooseModule(0, &out_batch);
  RunChooseModule(1, &drop_batch);
}

ADD_MODULE(UpdateTTL, "update_ttl", "decreases the IP TTL field by 1")
