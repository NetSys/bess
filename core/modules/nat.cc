#include "nat.h"

#include <rte_ip.h>

#include <algorithm>
#include <numeric>
#include <string>

#include "../utils/ether.h"
#include "../utils/icmp.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"

using bess::utils::EthHeader;
using bess::utils::Ipv4Header;
using bess::utils::UdpHeader;
using bess::utils::TcpHeader;
using bess::utils::IcmpHeader;

const uint16_t MIN_PORT = 1024;
const uint16_t MAX_PORT = 65535;
const uint64_t TIME_OUT_NS = 120L * 1000 * 1000 * 1000;

enum Protocol {
  ICMP = 0x01,
  TCP = 0x06,
  UDP = 0x11,
};

const Commands NAT::cmds = {
    {"add", "NATArg", MODULE_CMD_FUNC(&NAT::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&NAT::CommandClear), 0}};

pb_error_t NAT::Init(const bess::pb::NATArg &arg) {
  for (const auto &rule : arg.rules()) {
    CIDRNetwork int_net(rule.internal_addr_block());
    CIDRNetwork ext_net(rule.external_addr_block());
    rules_.push_back(std::make_pair(int_net, ext_net));
  }

  flow_hash_.Init(sizeof(Flow), sizeof(FlowRecord *));

  available_ports_.resize(MAX_PORT - MIN_PORT + 1);
  std::iota(available_ports_.begin(), available_ports_.end(), MIN_PORT);
  std::random_shuffle(available_ports_.begin(), available_ports_.end());

  flow_vec_.resize(MAX_PORT - MIN_PORT + 1);
  return pb_errno(0);
}

void NAT::DeInit() {
  flow_hash_.Close();
}

pb_cmd_response_t NAT::CommandAdd(const bess::pb::NATArg &arg) {
  for (const auto &rule : arg.rules()) {
    CIDRNetwork int_net(rule.internal_addr_block());
    CIDRNetwork ext_net(rule.external_addr_block());
    rules_.push_back(std::make_pair(int_net, ext_net));
  }
  return pb_cmd_response_t();
}

pb_cmd_response_t NAT::CommandClear(const bess::pb::EmptyArg &) {
  rules_.clear();
  flow_hash_.Clear();

  available_ports_.resize(MAX_PORT - MIN_PORT + 1);
  std::iota(available_ports_.begin(), available_ports_.end(), MIN_PORT);
  std::random_shuffle(available_ports_.begin(), available_ports_.end());

  flow_vec_.resize(MAX_PORT - MIN_PORT + 1);
  return pb_cmd_response_t();
}

// Recompute IP and TCP/UDP/ICMP checksum
inline static void compute_cksum(struct Ipv4Header *ip, void *l4) {
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
inline static Flow parse_flow(struct Ipv4Header *ip, void *l4) {
  struct UdpHeader *udp = reinterpret_cast<struct UdpHeader *>(l4);
  struct IcmpHeader *icmp = reinterpret_cast<struct IcmpHeader *>(l4);
  Flow flow;

  flow.proto = ip->protocol;
  flow.src_ip = ip->src;
  flow.dst_ip = ip->dst;

  switch (ip->protocol) {
    case UDP:
    case TCP:
      flow.src_port = udp->src_port;
      flow.dst_port = udp->dst_port;
      flow.icmp_ident = 0;
      break;
    case ICMP:
      switch (icmp->type) {
        case 0:
        case 8:
        case 13:
        case 15:
        case 16:
          flow.src_port = 0;
          flow.dst_port = 0;
          flow.icmp_ident = icmp->ident;
          break;
        default:
          VLOG(1) << "Unknown icmp_type: " << icmp->type;
      }
      break;
    default:
      VLOG(1) << "Unknown protocol: " << ip->protocol;
  }
  return flow;
}

// Rewrite IP header and L4 header using flow
inline static void stamp_flow(struct Ipv4Header *ip, void *l4,
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
    default:
      VLOG(1) << "Unknown protocol: " << flow.proto;
  }
  compute_cksum(ip, l4);
}

void NAT::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t incoming_gate = get_igate();

  bess::PacketBatch out_batch;
  out_batch.clear();
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct EthHeader *eth = pkt->head_data<struct EthHeader *>();
    struct Ipv4Header *ip = reinterpret_cast<struct Ipv4Header *>(eth + 1);
    size_t ip_bytes = (ip->header_length) << 2;

    void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_bytes;

    // L4 protocol must be TCP, UDP, or ICMP
    if (ip->protocol != TCP && ip->protocol != UDP && ip->protocol != ICMP) {
      bess::Packet::Free(pkt);
      continue;
    }

    Flow flow = parse_flow(ip, l4);
    uint64_t now = ctx.current_ns();

    FlowRecord **res = flow_hash_.Get(&flow);
    if (res != nullptr) {
      FlowRecord *record_ptr = *res;
      if (now - record_ptr->time < TIME_OUT_NS) {
        // Entry exists and does not exceed timeout
        record_ptr->time = now;
        if (incoming_gate == 0) {
          stamp_flow(ip, l4, record_ptr->external_flow);
        } else {
          stamp_flow(ip, l4, record_ptr->internal_flow.ReverseFlow());
        }
        out_batch.add(pkt);
        continue;
      } else {
        // Reclaim expired record
        available_ports_.push_back(record_ptr->port);
        record_ptr->time = 0;
        if (incoming_gate == 0) {
          flow_hash_.Del(&flow);
          Flow rev_flow = record_ptr->external_flow.ReverseFlow();
          flow_hash_.Del(&rev_flow);
        } else {
          flow_hash_.Del(&flow);
          flow_hash_.Del(&(record_ptr->internal_flow));
        }
        next_expiry_ = now;
      }
    }

    if (incoming_gate == 1) {
      // Flow from external network, drop.
      bess::Packet::Free(pkt);
      continue;
    }

    const auto rule_it = std::find_if(
        rules_.begin(), rules_.end(),
        [&ip](const NATRule &rule) { return rule.first.Match(ip->src); });
    if (rule_it == rules_.end()) {
      // No rules found for this source IP address, drop.
      bess::Packet::Free(pkt);
      continue;
    }

    // Garbage collect
    if (available_ports_.empty() && now >= next_expiry_) {
      next_expiry_ = UINT64_MAX;
      for (auto &record : flow_vec_) {
        if (record.time != 0 && now - record.time >= TIME_OUT_NS) {
          available_ports_.push_back(record.port);
          record.time = 0;
          flow_hash_.Del(&(record.internal_flow));
          Flow rev_flow = record.external_flow.ReverseFlow();
          flow_hash_.Del(&rev_flow);
        } else if (record.time != 0) {
          next_expiry_ = std::min(next_expiry_, record.time + TIME_OUT_NS);
        }
      }
    }

    // Still not available ports, then drop
    if (available_ports_.empty()) {
      bess::Packet::Free(pkt);
      continue;
    }

    uint16_t new_port = available_ports_.back();
    available_ports_.pop_back();

    // Invariant: record.port == new_port == index + MIN_PORT
    FlowRecord *record = &flow_vec_[new_port - MIN_PORT];

    record->port = new_port;
    record->time = now;
    record->internal_flow = flow;    // Copy
    flow_hash_.Set(&flow, &record);  // Copy

    flow.src_ip = RandomIP(rule_it->second);
    if (flow.icmp_ident == 0) {
      flow.src_port = new_port;
    } else {
      flow.icmp_ident = new_port;
    }
    record->external_flow = flow;
    Flow rev_flow = flow.ReverseFlow();  // Copy
    flow_hash_.Set(&rev_flow, &record);  // Copy

    stamp_flow(ip, l4, flow);
    out_batch.add(pkt);
  }
  RunChooseModule(incoming_gate, &out_batch);
}

ADD_MODULE(NAT, "nat", "Network address translator")
