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

#include "vlan_push.h"

#include <cstring>

#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/simd.h"

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::Ethernet;

const Commands VLANPush::cmds = {
    {"set_tci", "VLANPushArg", MODULE_CMD_FUNC(&VLANPush::CommandSetTci),
     Command::THREAD_UNSAFE},
};

CommandResponse VLANPush::Init(const bess::pb::VLANPushArg &arg) {
  return CommandSetTci(arg);
}

CommandResponse VLANPush::CommandSetTci(const bess::pb::VLANPushArg &arg) {
  uint16_t tci = arg.tci();
  vlan_tag_ = be32_t((Ethernet::Type::kVlan << 16) | tci);
  qinq_tag_ = be32_t((Ethernet::Type::kQinQ << 16) | tci);
  return CommandSuccess();
}

/* the behavior is undefined if a packet is already double tagged */
void VLANPush::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  be32_t vlan_tag = vlan_tag_;
  be32_t qinq_tag = qinq_tag_;

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    char *new_head;

    if ((new_head = static_cast<char *>(pkt->prepend(4))) != nullptr) {
/* shift 12 bytes to the left by 4 bytes */
#if __SSE4_1__
      __m128i ethh;

      ethh = _mm_loadu_si128(reinterpret_cast<__m128i *>(new_head + 4));
      be16_t tpid(be16_t::swap(_mm_extract_epi16(ethh, 6)));

      ethh = _mm_insert_epi32(ethh, (tpid.value() == Ethernet::Type::kVlan)
                                        ? qinq_tag.raw_value()
                                        : vlan_tag.raw_value(),
                              3);

      _mm_storeu_si128(reinterpret_cast<__m128i *>(new_head), ethh);
#else
      be16_t tpid(*(uint16_t *)(new_head + 16));
      memmove(new_head, new_head + 4, 12);

      *(be32_t *)(new_head + 12) =
          (tpid.value() == Ethernet::Type::kVlan) ? qinq_tag : vlan_tag;
#endif
    }
  }

  RunNextModule(batch);
}

std::string VLANPush::GetDesc() const {
  uint32_t vlan_tag = vlan_tag_.value();

  return bess::utils::Format("PCP=%u DEI=%u VID=%u", (vlan_tag >> 13) & 0x0007,
                             (vlan_tag >> 12) & 0x0001, vlan_tag & 0x0fff);
}

ADD_MODULE(VLANPush, "vlan_push", "adds 802.1Q/802.11ad VLAN tag")
