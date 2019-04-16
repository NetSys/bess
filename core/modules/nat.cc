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

#include "nat.h"

#include <algorithm>
#include <numeric>
#include <string>

#include "../utils/checksum.h"
#include "../utils/common.h"
#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/icmp.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using IpProto = bess::utils::Ipv4::Proto;
using bess::utils::Udp;
using bess::utils::Tcp;
using bess::utils::Icmp;
using bess::utils::ChecksumIncrement16;
using bess::utils::ChecksumIncrement32;
using bess::utils::UpdateChecksumWithIncrement;
using bess::utils::UpdateChecksum16;

const Commands NAT::cmds = {
    {"get_initial_arg", "EmptyArg", MODULE_CMD_FUNC(&NAT::GetInitialArg),
     Command::THREAD_SAFE},
    {"get_runtime_config", "EmptyArg", MODULE_CMD_FUNC(&NAT::GetRuntimeConfig),
     Command::THREAD_SAFE},
    {"set_runtime_config", "EmptyArg", MODULE_CMD_FUNC(&NAT::SetRuntimeConfig),
     Command::THREAD_SAFE}};

// TODO(torek): move this to set/get runtime config
CommandResponse NAT::Init(const bess::pb::NATArg &arg) {
  // Check before committing any changes.
  for (const auto &address_range : arg.ext_addrs()) {
    for (const auto &range : address_range.port_ranges()) {
      if (range.begin() >= range.end() || range.begin() > UINT16_MAX ||
          range.end() > UINT16_MAX) {
        return CommandFailure(EINVAL, "Port range for address %s is malformed",
                              address_range.ext_addr().c_str());
      }
    }
  }

  for (const auto &address_range : arg.ext_addrs()) {
    auto ext_addr = address_range.ext_addr();
    be32_t addr;

    bool ret = bess::utils::ParseIpv4Address(ext_addr, &addr);
    if (!ret) {
      return CommandFailure(EINVAL, "invalid IP address %s", ext_addr.c_str());
    }

    ext_addrs_.push_back(addr);
    // Add a port range list
    std::vector<PortRange> port_list;
    if (address_range.port_ranges().size() == 0) {
      port_list.emplace_back(PortRange{
          .begin = 0u, .end = 65535u, .suspended = false,
      });
    }
    for (const auto &range : address_range.port_ranges()) {
      port_list.emplace_back(PortRange{
          .begin = (uint16_t)range.begin(),
          .end = (uint16_t)range.end(),
          // Control plane gets to decide if the port range can be used.
          .suspended = range.suspended()});
    }
    port_ranges_.push_back(port_list);
  }

  if (ext_addrs_.empty()) {
    return CommandFailure(EINVAL,
                          "at least one external IP address must be specified");
  }

  // Sort so that GetInitialArg is predictable and consistent.
  std::sort(ext_addrs_.begin(), ext_addrs_.end());

  return CommandSuccess();
}

CommandResponse NAT::GetInitialArg(const bess::pb::EmptyArg &) {
  bess::pb::NATArg resp;
  for (size_t i = 0; i < ext_addrs_.size(); i++) {
    auto ext = resp.add_ext_addrs();
    ext->set_ext_addr(ToIpv4Address(ext_addrs_[i]));
    for (auto irange : port_ranges_[i]) {
      auto erange = ext->add_port_ranges();
      erange->set_begin((uint32_t)irange.begin);
      erange->set_end((uint32_t)irange.end);
      erange->set_suspended(irange.suspended);
    }
  }
  return CommandSuccess(resp);
}

CommandResponse NAT::GetRuntimeConfig(const bess::pb::EmptyArg &) {
  return CommandSuccess();
}

CommandResponse NAT::SetRuntimeConfig(const bess::pb::EmptyArg &) {
  return CommandSuccess();
}

static inline std::pair<bool, Endpoint> ExtractEndpoint(const Ipv4 *ip,
                                                        const void *l4,
                                                        NAT::Direction dir) {
  IpProto proto = static_cast<IpProto>(ip->protocol);

  if (likely(proto == IpProto::kTcp || proto == IpProto::kUdp)) {
    // UDP and TCP share the same layout for port numbers
    const Udp *udp = static_cast<const Udp *>(l4);
    Endpoint ret;

    if (dir == NAT::kForward) {
      ret = {.addr = ip->src, .port = udp->src_port, .protocol = proto};
    } else {
      ret = {.addr = ip->dst, .port = udp->dst_port, .protocol = proto};
    }

    return std::make_pair(true, ret);
  }

  // slow path
  if (proto == IpProto::kIcmp) {
    const Icmp *icmp = static_cast<const Icmp *>(l4);
    Endpoint ret;

    if (icmp->type == 0 || icmp->type == 8 || icmp->type == 13 ||
        icmp->type == 15 || icmp->type == 16) {
      if (dir == NAT::kForward) {
        ret = {
            .addr = ip->src, .port = icmp->ident, .protocol = IpProto::kIcmp};
      } else {
        ret = {
            .addr = ip->dst, .port = icmp->ident, .protocol = IpProto::kIcmp};
      }

      return std::make_pair(true, ret);
    }
  }

  return std::make_pair(
      false, Endpoint{.addr = ip->src, .port = be16_t(0), .protocol = 0});
}

// Not necessary to inline this function, since it is less frequently called
NAT::HashTable::Entry *NAT::CreateNewEntry(const Endpoint &src_internal,
                                           uint64_t now) {
  Endpoint src_external;

  // An internal IP address is always mapped to the same external IP address,
  // in an deterministic manner (rfc4787 REQ-2)
  size_t hashed = rte_hash_crc(&src_internal.addr, sizeof(be32_t), 0);
  size_t ext_addr_index = hashed % ext_addrs_.size();
  src_external.addr = ext_addrs_[ext_addr_index];
  src_external.protocol = src_internal.protocol;

  for (const auto &port_range : port_ranges_[ext_addr_index]) {
    uint16_t min;
    uint16_t range;  // consider [min, min + range) port range
    // Avoid allocation from an unusable range. We do this even when a range is
    // already in use since we might want to reclaim it once flows die out.
    if (port_range.suspended) {
      continue;
    }

    if (src_internal.protocol == IpProto::kIcmp) {
      min = port_range.begin;
      range = port_range.end - port_range.begin;
    } else {
      if (src_internal.port == be16_t(0)) {
        // ignore port number 0
        return nullptr;
      } else if (src_internal.port & ~be16_t(1023)) {
        if (port_range.end <= 1024u) {
          continue;
        }
        min = std::max((uint16_t)1024, port_range.begin);
        range = port_range.end - min + 1;
      } else {
        // Privileged ports are mapped to privileged ports (rfc4787 REQ-5-a)
        if (port_range.begin >= 1023u) {
          continue;
        }
        min = port_range.begin;
        range = std::min((uint16_t)1023, port_range.end) - min;
      }
    }

    // Start from a random port, then do linear probing
    uint16_t start_port = min + rng_.GetRange(range);
    uint16_t port = start_port;
    int trials = 0;

    do {
      src_external.port = be16_t(port);
      auto *hash_reverse = map_.Find(src_external);
      if (hash_reverse == nullptr) {
      found:
        // Found an available src_internal <-> src_external mapping
        NatEntry forward_entry;
        NatEntry reverse_entry;

        reverse_entry.endpoint = src_internal;
        map_.Insert(src_external, reverse_entry);

        forward_entry.endpoint = src_external;
        return map_.Insert(src_internal, forward_entry);
      } else {
        // A':a' is not free, but it might have been expired.
        // Check with the forward hash entry since timestamp refreshes only for
        // forward direction.
        auto *hash_forward = map_.Find(hash_reverse->second.endpoint);

        // Forward and reverse entries must share the same lifespan.
        DCHECK(hash_forward != nullptr);

        if (now - hash_forward->second.last_refresh > kTimeOutNs) {
          // Found an expired mapping. Remove A':a' <-> A'':a''...
          map_.Remove(hash_forward->first);
          map_.Remove(hash_reverse->first);
          goto found;  // and go install A:a <-> A':a'
        }
      }

      port++;
      trials++;

      // Out of range? Also check if zero due to uint16_t overflow
      if (port == 0 || port >= min + range) {
        port = min;
      }
      // FIXME: Should not try for kMaxTrials.
    } while (port != start_port && trials < kMaxTrials);
  }
  return nullptr;
}

template <NAT::Direction dir>
inline void Stamp(Ipv4 *ip, void *l4, const Endpoint &before,
                  const Endpoint &after) {
  IpProto proto = static_cast<IpProto>(ip->protocol);
  DCHECK_EQ(before.protocol, after.protocol);
  DCHECK_EQ(before.protocol, proto);

  if (dir == NAT::kForward) {
    ip->src = after.addr;
  } else {
    ip->dst = after.addr;
  }

  uint32_t l3_increment =
      ChecksumIncrement32(before.addr.raw_value(), after.addr.raw_value());
  ip->checksum = UpdateChecksumWithIncrement(ip->checksum, l3_increment);

  uint32_t l4_increment =
      l3_increment +
      ChecksumIncrement16(before.port.raw_value(), after.port.raw_value());

  if (likely(proto == IpProto::kTcp || proto == IpProto::kUdp)) {
    Udp *udp = static_cast<Udp *>(l4);
    if (dir == NAT::kForward) {
      udp->src_port = after.port;
    } else {
      udp->dst_port = after.port;
    }

    if (proto == IpProto::kTcp) {
      Tcp *tcp = static_cast<Tcp *>(l4);
      tcp->checksum = UpdateChecksumWithIncrement(tcp->checksum, l4_increment);
    } else {
      // NOTE: UDP checksum is tricky in two ways:
      // 1. if the old checksum field was 0 (not set), no need to update
      // 2. if the updated value is 0, use 0xffff (rfc768)
      if (udp->checksum != 0) {
        udp->checksum =
            UpdateChecksumWithIncrement(udp->checksum, l4_increment) ?: 0xffff;
      }
    }
  } else {
    DCHECK_EQ(proto, IpProto::kIcmp);
    Icmp *icmp = static_cast<Icmp *>(l4);
    icmp->ident = after.port;

    // ICMP does not have a pseudo header
    icmp->checksum = UpdateChecksum16(icmp->checksum, before.port.raw_value(),
                                      after.port.raw_value());
  }
}

template <NAT::Direction dir>
inline void NAT::DoProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  gate_idx_t ogate_idx = dir == kForward ? 1 : 0;
  int cnt = batch->cnt();
  uint64_t now = ctx->current_ns;

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
    size_t ip_bytes = (ip->header_length) << 2;
    void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_bytes;

    bool valid_protocol;
    Endpoint before;
    std::tie(valid_protocol, before) = ExtractEndpoint(ip, l4, dir);

    if (!valid_protocol) {
      DropPacket(ctx, pkt);
      continue;
    }

    auto *hash_item = map_.Find(before);

    if (hash_item == nullptr) {
      if (dir != kForward || !(hash_item = CreateNewEntry(before, now))) {
        DropPacket(ctx, pkt);
        continue;
      }
    }

    // only refresh for outbound packets, rfc4787 REQ-6
    if (dir == kForward) {
      hash_item->second.last_refresh = now;
    }

    Stamp<dir>(ip, l4, before, hash_item->second.endpoint);
    EmitPacket(ctx, pkt, ogate_idx);
  }
}

void NAT::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  gate_idx_t incoming_gate = ctx->current_igate;

  if (incoming_gate == 0) {
    DoProcessBatch<kForward>(ctx, batch);
  } else {
    DoProcessBatch<kReverse>(ctx, batch);
  }
}

std::string NAT::GetDesc() const {
  // Divide by 2 since the table has both forward and reverse entries
  return bess::utils::Format("%zu entries", map_.Count() / 2);
}

ADD_MODULE(NAT, "nat", "Dynamic Network address/port translator")
