#include "nat.h"

#include <rte_ip.h>

#include <algorithm>
#include <numeric>
#include <string>

#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/icmp.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"

using bess::utils::EthHeader;
using bess::utils::Ipv4Header;
using bess::utils::UdpHeader;
using bess::utils::TcpHeader;
using bess::utils::IcmpHeader;

const Commands NAT::cmds = {
    {"add", "NATArg", MODULE_CMD_FUNC(&NAT::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&NAT::CommandClear), 0}};

std::string Flow::ToString() const {
  return bess::utils::Format("%d %08x:%d -> %08x:%d", proto, src_ip, src_port,
                             dst_ip, dst_port);
}

pb_error_t NAT::Init(const bess::pb::NATArg &arg) {
  InitRules(arg);
  return pb_errno(0);
}

pb_cmd_response_t NAT::CommandAdd(const bess::pb::NATArg &arg) {
  InitRules(arg);
  return pb_cmd_response_t();
}

pb_cmd_response_t NAT::CommandClear(const bess::pb::EmptyArg &) {
  rules_.clear();
  flow_hash_.Clear();

  return pb_cmd_response_t();
}

// Recompute IP and TCP/UDP/ICMP checksum
static inline void compute_cksum(struct Ipv4Header *ip, void *l4) {
  struct TcpHeader *tcp = reinterpret_cast<struct TcpHeader *>(l4);
  struct UdpHeader *udp = reinterpret_cast<struct UdpHeader *>(l4);
  struct IcmpHeader *icmp = reinterpret_cast<struct IcmpHeader *>(l4);

  ip->checksum = 0;
  switch (ip->protocol) {
    case TCP:
      tcp->checksum = 0;
      tcp->checksum =
          rte_ipv4_udptcp_cksum(reinterpret_cast<const ipv4_hdr *>(ip), tcp);
      break;
    case UDP:
      udp->checksum = 0;
      break;
    case ICMP:
      icmp->checksum = 0;
      icmp->checksum =
          rte_ipv4_udptcp_cksum(reinterpret_cast<const ipv4_hdr *>(ip), icmp);
      break;
    default:
      VLOG(1) << "Unknown protocol: " << ip->protocol;
  }
  ip->checksum = rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip));
}

// Extract a Flow object from IP header ip and L4 header l4
static inline Flow parse_flow(struct Ipv4Header *ip, void *l4) {
  struct UdpHeader *udp = reinterpret_cast<struct UdpHeader *>(l4);
  struct IcmpHeader *icmp = reinterpret_cast<struct IcmpHeader *>(l4);
  Flow flow;

  flow.proto = ip->protocol;
  flow.src_ip = ip->src;
  flow.dst_ip = ip->dst;

  switch (flow.proto) {
    case UDP:
    case TCP:
      flow.src_port = udp->src_port;
      flow.dst_port = udp->dst_port;
      break;
    case ICMP:
      switch (icmp->type) {
        case 0:
        case 8:
        case 13:
        case 15:
        case 16:
          flow.icmp_ident = icmp->ident;
          break;
        default:
          VLOG(1) << "Unknown icmp_type: " << icmp->type;
      }
      break;
  }
  return flow;
}

// Rewrite IP header and L4 header using flow
static inline void stamp_flow(struct Ipv4Header *ip, void *l4,
                              const Flow &flow) {
  struct UdpHeader *udp = reinterpret_cast<struct UdpHeader *>(l4);
  struct IcmpHeader *icmp = reinterpret_cast<struct IcmpHeader *>(l4);

  ip->src = flow.src_ip;
  ip->dst = flow.dst_ip;

  switch (flow.proto) {
    case UDP:
    case TCP:
      udp->src_port = flow.src_port;
      udp->dst_port = flow.dst_port;
      break;
    case ICMP:
      switch (icmp->type) {
        case 0:
        case 8:
        case 13:
        case 15:
        case 16:
          icmp->ident = flow.icmp_ident;
          break;
        default:
          VLOG(1) << "Unknown icmp_type: " << icmp->type;
      }
      break;
  }
  compute_cksum(ip, l4);
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

    struct EthHeader *eth = pkt->head_data<struct EthHeader *>();
    struct Ipv4Header *ip = reinterpret_cast<struct Ipv4Header *>(eth + 1);
    size_t ip_bytes = (ip->header_length) << 2;

    void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_bytes;

    Flow flow = parse_flow(ip, l4);

    // L4 protocol must be TCP, UDP, or ICMP
    if (ip->protocol != TCP && ip->protocol != UDP && ip->protocol != ICMP) {
      free_batch.add(pkt);
      continue;
    }

    const auto rule_it =
        std::find_if(rules_.begin(), rules_.end(),
                     [&ip](const std::pair<CIDRNetwork, AvailablePorts> &rule) {
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
            stamp_flow(ip, l4, record->external_flow);
          } else {
            stamp_flow(ip, l4, record->internal_flow.ReverseFlow());
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

    IPAddress new_ip;
    uint16_t new_port;
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

    stamp_flow(ip, l4, flow);
    out_batch.add(pkt);
  }

  bess::Packet::Free(&free_batch);

  RunChooseModule(incoming_gate, &out_batch);
}

ADD_MODULE(NAT, "nat", "Network address translator")
