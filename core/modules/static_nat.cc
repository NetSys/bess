// Copyright (c) 2018, Nefeli Networks, Inc.
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

#include "static_nat.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"

const Commands StaticNAT::cmds = {
    {"get_initial_arg", "EmptyArg", MODULE_CMD_FUNC(&StaticNAT::GetInitialArg),
     Command::THREAD_SAFE},
    {"get_runtime_config", "EmptyArg",
     MODULE_CMD_FUNC(&StaticNAT::GetRuntimeConfig), Command::THREAD_SAFE},
    {"set_runtime_config", "EmptyArg",
     MODULE_CMD_FUNC(&StaticNAT::SetRuntimeConfig), Command::THREAD_SAFE}};

CommandResponse StaticNAT::Init(const bess::pb::StaticNATArg &arg) {
  using bess::utils::ParseIpv4Address;

  for (const auto &pb_pair : arg.pairs()) {
    be32_t int_start, int_end;
    if (!ParseIpv4Address(pb_pair.int_range().start(), &int_start)) {
      return CommandFailure(EINVAL, "invalid IP address %s",
                            pb_pair.int_range().start().c_str());
    }
    if (!ParseIpv4Address(pb_pair.int_range().end(), &int_end)) {
      return CommandFailure(EINVAL, "invalid IP address %s",
                            pb_pair.int_range().end().c_str());
    }
    if (int_start > int_end) {
      return CommandFailure(EINVAL, "invalid internal IP address range");
    }

    be32_t ext_start, ext_end;
    if (!ParseIpv4Address(pb_pair.ext_range().start(), &ext_start)) {
      return CommandFailure(EINVAL, "invalid IP address %s",
                            pb_pair.ext_range().start().c_str());
    }
    if (!ParseIpv4Address(pb_pair.ext_range().end(), &ext_end)) {
      return CommandFailure(EINVAL, "invalid IP address %s",
                            pb_pair.ext_range().end().c_str());
    }
    if (ext_start > ext_end) {
      return CommandFailure(EINVAL, "invalid external IP address range");
    }

    if (int_end.value() == 0xffffffff || ext_end.value() == 0xffffffff) {
      return CommandFailure(EINVAL, "cannot map broadcast address");
    }

    if (int_end - int_start != ext_end - ext_start) {
      return CommandFailure(EINVAL, "internal/external address ranges differ");
    }

    pairs_.push_back({.int_addr = int_start.value(),
                      .ext_addr = ext_start.value(),
                      .size = int_end.value() - int_start.value() + 1});
  }

  return CommandSuccess();
}

CommandResponse StaticNAT::GetInitialArg(const bess::pb::EmptyArg &) {
  using bess::utils::ToIpv4Address;

  bess::pb::StaticNATArg resp;
  for (const auto &pair : pairs_) {
    auto *pb_pair = resp.add_pairs();

    auto *int_range = pb_pair->mutable_int_range();
    int_range->set_start(ToIpv4Address(be32_t(pair.int_addr)));
    int_range->set_end(ToIpv4Address(be32_t(pair.int_addr + pair.size)));

    auto *ext_range = pb_pair->mutable_ext_range();
    ext_range->set_start(ToIpv4Address(be32_t(pair.ext_addr)));
    ext_range->set_end(ToIpv4Address(be32_t(pair.ext_addr + pair.size)));
  }

  return CommandSuccess(resp);
}

CommandResponse StaticNAT::GetRuntimeConfig(const bess::pb::EmptyArg &) {
  return CommandSuccess();
}

CommandResponse StaticNAT::SetRuntimeConfig(const bess::pb::EmptyArg &) {
  return CommandSuccess();
}

// Updates L3 (and L4, if necessary) checksum
static inline void UpdateChecksum(bess::utils::Ipv4 *ip, uint32_t incr) {
  using IpProto = bess::utils::Ipv4::Proto;

  size_t ip_bytes = (ip->header_length) << 2;
  void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_bytes;
  IpProto proto = static_cast<IpProto>(ip->protocol);

  ip->checksum = bess::utils::UpdateChecksumWithIncrement(ip->checksum, incr);

  if (proto == IpProto::kTcp) {
    auto *tcp = static_cast<bess::utils::Tcp *>(l4);
    tcp->checksum =
        bess::utils::UpdateChecksumWithIncrement(tcp->checksum, incr);
  } else if (proto == IpProto::kUdp) {
    // NOTE: UDP checksum is tricky in two ways:
    // 1. if the old checksum field was 0 (not set), no need to update
    // 2. if the updated value is 0, use 0xffff (rfc768)
    auto *udp = static_cast<bess::utils::Udp *>(l4);
    if (udp->checksum != 0) {
      udp->checksum =
          bess::utils::UpdateChecksumWithIncrement(udp->checksum, incr)
              ?: 0xffff;
    }
  }
}

template <StaticNAT::Direction dir>
inline void StaticNAT::DoProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  gate_idx_t ogate_idx = dir == kForward ? 1 : 0;
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    auto *eth = pkt->head_data<bess::utils::Ethernet *>();
    auto *ip = reinterpret_cast<bess::utils::Ipv4 *>(eth + 1);

    be32_t &addr_be = (dir == kForward) ? ip->src : ip->dst;
    uint32_t addr = addr_be.value();

    for (const auto &pair : pairs_) {
      uint32_t start_addr = (dir == kForward) ? pair.int_addr : pair.ext_addr;
      if (start_addr <= addr && addr < start_addr + pair.size) {
        uint32_t diff = (dir == kForward) ? (pair.ext_addr - pair.int_addr)
                                          : (pair.int_addr - pair.ext_addr);
        be32_t new_addr_be = be32_t(addr + diff);
        UpdateChecksum(ip, bess::utils::ChecksumIncrement32(
                               addr_be.raw_value(), new_addr_be.raw_value()));
        addr_be = new_addr_be;
        break;
      }
    }

    // If there is no matching address pair, forward without NAT.
    // TODO: add an option to redirect unmatched traffic to a specified ogate

    EmitPacket(ctx, pkt, ogate_idx);
  }
}

void StaticNAT::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  gate_idx_t incoming_gate = ctx->current_igate;

  if (incoming_gate == 0) {
    DoProcessBatch<kForward>(ctx, batch);
  } else {
    DoProcessBatch<kReverse>(ctx, batch);
  }
}

ADD_MODULE(StaticNAT, "static_nat", "Static network address translator")
