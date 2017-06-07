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

CommandResponse NAT::Init(const bess::pb::NATArg &arg) {
  for (const std::string &ext_addr : arg.ext_addrs()) {
    be32_t addr;
    bool ret = bess::utils::ParseIpv4Address(ext_addr, &addr);
    if (!ret) {
      return CommandFailure(EINVAL, "invalid IP address %s", ext_addr.c_str());
    }

    ext_addrs_.push_back(addr);
  }

  if (ext_addrs_.empty()) {
    return CommandFailure(EINVAL,
                          "at least one external IP address must be specified");
  }

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
  src_external.addr = ext_addrs_[hashed % ext_addrs_.size()];
  src_external.protocol = src_internal.protocol;

  uint16_t min;
  uint16_t range;  // consider [min, min + range) port range

  if (src_internal.protocol == IpProto::kIcmp) {
    min = 0;
    range = 65535;  // identifier 65535 won't be used, but who cares?
  } else {
    if (src_internal.port == be16_t(0)) {
      // ignore port number 0
      return nullptr;
    } else if (src_internal.port & ~be16_t(1023)) {
      min = 1024;
      range = 65535 - min + 1;
    } else {
      // Privileged ports are mapped to privileged ports (rfc4787 REQ-5-a)
      min = 1;
      range = 1023;
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
  } while (port != start_port && trials < kMaxTrials);

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
inline void NAT::DoProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch out_batch;
  bess::PacketBatch free_batch;
  out_batch.clear();
  free_batch.clear();

  int cnt = batch->cnt();
  uint64_t now = ctx.current_ns();

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
      free_batch.add(pkt);
      continue;
    }

    auto *hash_item = map_.Find(before);

    if (hash_item == nullptr) {
      if (dir != kForward || !(hash_item = CreateNewEntry(before, now))) {
        free_batch.add(pkt);
        continue;
      }
    }

    // only refresh for outbound packets, rfc4787 REQ-6
    if (dir == kForward) {
      hash_item->second.last_refresh = now;
    }

    Stamp<dir>(ip, l4, before, hash_item->second.endpoint);

    out_batch.add(pkt);
  }

  bess::Packet::Free(&free_batch);

  RunChooseModule(static_cast<gate_idx_t>(dir), &out_batch);
}

void NAT::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t incoming_gate = get_igate();

  if (incoming_gate == 0) {
    DoProcessBatch<kForward>(batch);
  } else {
    DoProcessBatch<kReverse>(batch);
  }
}

std::string NAT::GetDesc() const {
  // Divide by 2 since the table has both forward and reverse entries
  return bess::utils::Format("%zu entries", map_.Count() / 2);
}

ADD_MODULE(NAT, "nat", "Network address translator")
