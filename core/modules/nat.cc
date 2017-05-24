#include "nat.h"

#include <algorithm>
#include <numeric>
#include <string>

#include "../utils/checksum.h"
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
using bess::utils::UpdateChecksum16;
using bess::utils::ChecksumIncrement16;
using bess::utils::ChecksumIncrement32;
using bess::utils::UpdateChecksumWithIncrement;

const Commands NAT::cmds = {
    {"add", "NATArg", MODULE_CMD_FUNC(&NAT::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&NAT::CommandClear), 0}};

inline Flow Flow::ReverseFlow() const {
  if (proto == IpProto::kIcmp) {
    return Flow(dst_ip, src_ip, icmp_ident, be16_t(0), IpProto::kIcmp);
  } else {
    return Flow(dst_ip, src_ip, dst_port, src_port, proto);
  }
}

std::string Flow::ToString() const {
  return bess::utils::Format("%d %08x:%hu -> %08x:%hu", proto, src_ip.value(),
                             src_port.value(), dst_ip.value(),
                             dst_port.value());
}

CommandResponse NAT::Init(const bess::pb::NATArg &arg) {
  InitRules(arg);
  return CommandSuccess();
}

CommandResponse NAT::CommandAdd(const bess::pb::NATArg &arg) {
  InitRules(arg);
  return CommandResponse();
}

CommandResponse NAT::CommandClear(const bess::pb::EmptyArg &) {
  rules_.clear();
  flow_hash_.Clear();

  return CommandResponse();
}

// Extract a Flow object from IP header ip and L4 header l4
static inline Flow parse_flow(Ipv4 *ip, void *l4) {
  Udp *udp = reinterpret_cast<Udp *>(l4);
  Icmp *icmp = reinterpret_cast<Icmp *>(l4);
  Flow flow;

  flow.proto = ip->protocol;
  flow.src_ip = ip->src;
  flow.dst_ip = ip->dst;

  switch (flow.proto) {
    case IpProto::kTcp:
    case IpProto::kUdp:
      flow.src_port = udp->src_port;
      flow.dst_port = udp->dst_port;
      break;
    case IpProto::kIcmp:
      switch (icmp->type) {
        case 0:
        case 8:
        case 13:
        case 15:
        case 16:
          flow.icmp_ident = icmp->ident;
          flow.dst_port = be16_t(0);
          break;
        default:
          VLOG(1) << "Unknown icmp_type: " << icmp->type;
      }
      break;
  }
  return flow;
}

template <bool src>
static inline void stamp_flow(Ipv4 *ip, void *l4, const Flow &flow) {
  Udp *udp = reinterpret_cast<Udp *>(l4);
  Tcp *tcp = reinterpret_cast<Tcp *>(l4);
  Icmp *icmp = reinterpret_cast<Icmp *>(l4);
  uint32_t l3_inc = 0;
  uint32_t l4_inc = 0;

  if (src) {
    l3_inc += ChecksumIncrement32(ip->src.raw_value(),
                                                     flow.src_ip.raw_value());
    ip->src = flow.src_ip;
  } else {
    l3_inc += ChecksumIncrement32(ip->dst.raw_value(),
                                                     flow.dst_ip.raw_value());
    ip->dst = flow.dst_ip;
  }

  ip->checksum = UpdateChecksumWithIncrement(ip->checksum, l3_inc);

  switch (flow.proto) {
    case IpProto::kTcp:
      if (src) {
        l4_inc += ChecksumIncrement16(
            tcp->src_port.raw_value(), flow.src_port.raw_value());
        tcp->src_port = flow.src_port;
      } else {
        l4_inc += ChecksumIncrement16(
            tcp->dst_port.raw_value(), flow.dst_port.raw_value());
        tcp->dst_port = flow.dst_port;
      }
      l4_inc += l3_inc;

      tcp->checksum = UpdateChecksumWithIncrement(tcp->checksum, l4_inc);
      break;

    case IpProto::kUdp:
      if (udp->checksum) {
        if (src) {
          l4_inc += ChecksumIncrement16(
              udp->src_port.raw_value(), flow.src_port.raw_value());
        } else {
          l4_inc += ChecksumIncrement16(
              udp->dst_port.raw_value(), flow.dst_port.raw_value());
        }
        l4_inc += l3_inc;

        udp->checksum = UpdateChecksumWithIncrement(udp->checksum, l4_inc);

        if (!udp->checksum) {
          udp->checksum = 0xFFFF;
        }
      }

      if (src) {
        udp->src_port = flow.src_port;
      } else {
        udp->dst_port = flow.dst_port;
      }
      break;

    case IpProto::kIcmp:
      switch (icmp->type) {
        case 0:
        case 8:
        case 13:
        case 15:
        case 16:
          icmp->checksum =
              UpdateChecksum16(icmp->checksum, icmp->ident.raw_value(),
                               flow.icmp_ident.raw_value());
          icmp->ident = flow.icmp_ident;
          break;
        default:
          VLOG(1) << "Unknown icmp_type: " << icmp->type;
      }
      break;
  }
}

// Rewrite IP header and L4 header src info using flow
static inline void stamp_flow_src(Ipv4 *ip, void *l4, const Flow &flow) {
  stamp_flow<true>(ip, l4, flow);
}

// Rewrite IP header and L4 header dst info using flow
static inline void stamp_flow_dst(Ipv4 *ip, void *l4, const Flow &flow) {
  stamp_flow<false>(ip, l4, flow);
}

void NAT::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t incoming_gate = get_igate();

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

    Flow flow = parse_flow(ip, l4);

    if (ip->protocol != IpProto::kTcp && ip->protocol != IpProto::kUdp &&
        ip->protocol != IpProto::kIcmp) {
      free_batch.add(pkt);
      continue;
    }

    const auto rule_it =
        std::find_if(rules_.begin(), rules_.end(),
                     [&ip](const std::pair<Ipv4Prefix, AvailablePorts> &rule) {
                       return rule.first.Match(ip->src);
                     });

    {
      auto *res = flow_hash_.Find(flow);
      if (res != nullptr) {
        FlowRecord *record = res->second;
        DCHECK_EQ(record->external_flow.src_port, record->port);

        if (now - record->time < TIME_OUT_NS) {
          // Entry exists and does not exceed timeout
          record->time = now;
          if (incoming_gate == 0) {
            stamp_flow_src(ip, l4, record->external_flow);
          } else {
            stamp_flow_dst(ip, l4, record->internal_flow.ReverseFlow());
          }
          out_batch.add(pkt);
          continue;
        } else {
          // Reclaim expired record
          record->time = 0;
          if (incoming_gate == 0) {
            flow_hash_.Remove(flow);
            Flow rev_flow = record->external_flow.ReverseFlow();
            flow_hash_.Remove(rev_flow);
          } else {
            flow_hash_.Remove(flow);
            flow_hash_.Remove(record->internal_flow);
          }
          AvailablePorts &available_ports = rule_it->second;
          available_ports.FreeAllocated(std::make_tuple(
              record->external_flow.src_ip, record->port, record));
        }
      }
    }

    // The flow didn't match, and we currently don't support any mechanisms to
    // allow external flows entry through the NAT.
    if (incoming_gate == 1) {
      // Flow from external network, drop.
      free_batch.add(pkt);
      continue;
    }

    // The flow must be a new flow if we have gotten this far.  So look for a
    // rule that tells us what external prefix this packet's flow maps to.
    if (rule_it == rules_.end()) {
      // No rules found for this source IP address, drop.
      free_batch.add(pkt);
      continue;
    }

    AvailablePorts &available_ports = rule_it->second;

    // Garbage collect.
    if (available_ports.empty() && now >= available_ports.next_expiry()) {
      uint64_t expiry = UINT64_MAX;

      for (auto it = flow_hash_.begin(); it != flow_hash_.end(); ++it) {
        FlowRecord *record = it->second;
        if (record->time != 0 && (now - record->time) >= TIME_OUT_NS) {
          // Found expired flow entry.
          record->time = 0;
          flow_hash_.Remove(record->internal_flow);
          Flow rev_flow = record->external_flow.ReverseFlow();
          flow_hash_.Remove(rev_flow);
          available_ports.FreeAllocated(
              std::make_tuple(record->external_flow.src_ip,
                              record->external_flow.src_port, record));
        } else if (record->time != 0) {
          expiry = std::min(expiry, record->time + TIME_OUT_NS);
        }
      }
      available_ports.set_next_expiry(expiry);
    }

    // Still no available ports, so drop.
    if (available_ports.empty()) {
      free_batch.add(pkt);
      continue;
    }

    be32_t new_ip;
    be16_t new_port;
    FlowRecord *record;
    std::tie(new_ip, new_port, record) = available_ports.RandomFreeIPAndPort();

    record->port = new_port;
    record->time = now;
    record->internal_flow = flow;     // Copy
    flow_hash_.Insert(flow, record);  // Copy

    flow.src_ip = new_ip;
    flow.src_port = new_port;

    record->external_flow = flow;
    Flow rev_flow = flow.ReverseFlow();   // Copy
    flow_hash_.Insert(rev_flow, record);  // Copy

    stamp_flow_src(ip, l4, flow);
    out_batch.add(pkt);
  }

  bess::Packet::Free(&free_batch);

  RunChooseModule(incoming_gate, &out_batch);
}

ADD_MODULE(NAT, "nat", "Network address translator")
