#include "nat.h"

#include <rte_ether.h>
#include <rte_icmp.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include <numeric>
#include <string>

const uint16_t MIN_PORT = 1024;
const uint16_t MAX_PORT = 65535;
const uint64_t TIME_OUT = 12e10;

const Commands NAT::cmds = {
    {"add", "NATArg", MODULE_CMD_FUNC(&NAT::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&NAT::CommandClear), 0}};

pb_error_t NAT::Init(const bess::pb::NATArg &arg) {
  for (const auto &rule : arg.rules()) {
    CIDRNetwork int_net(rule.internal_addr_block());
    CIDRNetwork ext_net(rule.external_addr_block());
    rules_.push_back(std::make_pair(int_net, ext_net));
  }

  available_ports_.reserve(MAX_PORT - MIN_PORT + 1);
  std::iota(available_ports_.begin(), available_ports_.end(), MIN_PORT);

  flow_vec_.reserve(MAX_PORT - MIN_PORT + 1);

  return pb_errno(0);
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
  flow_hash_.clear();
  flow_vec_.clear();
  available_ports_.reserve(MAX_PORT - MIN_PORT + 1);
  std::iota(available_ports_.begin(), available_ports_.end(), MIN_PORT);
  return pb_cmd_response_t();
}

inline static void compute_cksum(struct ipv4_hdr *ip, void *l4) {
  struct tcp_hdr *tcp = reinterpret_cast<struct tcp_hdr *>(l4);
  struct udp_hdr *udp = reinterpret_cast<struct udp_hdr *>(l4);
  struct icmp_hdr *icmp = reinterpret_cast<struct icmp_hdr *>(l4);

  ip->hdr_checksum = 0;
  switch (ip->next_proto_id) {
    case 0x06:
      tcp->cksum = 0;
      tcp->cksum = rte_ipv4_udptcp_cksum(ip, tcp);
      break;
    case 0x11:
      udp->dgram_cksum = 0;
      udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip, udp);
      break;
    case 0x01:
      icmp->icmp_cksum = 0;
      icmp->icmp_cksum = rte_ipv4_udptcp_cksum(ip, icmp);
      break;
  }
  ip->hdr_checksum = rte_ipv4_cksum(ip);
}

void NAT::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  gate_idx_t incoming_gate = get_igate();

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    // By default, drop packet
    out_gates[i] = DROP_GATE;

    struct ether_hdr *eth = pkt->head_data<struct ether_hdr *>();
    struct ipv4_hdr *ip = reinterpret_cast<struct ipv4_hdr *>(eth + 1);
    int ip_bytes = (ip->version_ihl & 0xf) << 2;
    struct udp_hdr *udp = reinterpret_cast<struct udp_hdr *>(
        reinterpret_cast<uint8_t *>(ip) + ip_bytes);

    // L4 protocol must be TCP, UDP, or ICMP
    if (ip->next_proto_id != 0x06 && ip->next_proto_id != 0x11 &&
        ip->next_proto_id != 0x01) {
      out_gates[i] = DROP_GATE;
    }

    NAT::Flow flow = {.src_ip = ip->src_addr,
                      .src_port = udp->src_port,
                      .dst_ip = ip->dst_addr,
                      .dst_port = udp->dst_port};

    uint64_t now = tsc_to_ns(rdtsc());
    auto hash_it = flow_hash_.find(flow);

    if (hash_it != flow_hash_.end() && now - hash_it->second.time < TIME_OUT) {
      // Entry exists and does not exceed timeout
      const NAT::Flow &rev_flow = (incoming_gate == 0)
                                      ? hash_it->second.reverse_flow
                                      : hash_it->second.forward_flow;
      hash_it->second.time = now;
      ip->src_addr = rev_flow.src_ip;
      ip->dst_addr = rev_flow.dst_ip;
      udp->src_port = rev_flow.src_port;
      udp->dst_port = rev_flow.dst_port;
      ip->hdr_checksum = 0;
      compute_cksum(ip, udp);
    } else {
      // Reclaim expired record
      if (hash_it != flow_hash_.end() &&
          now - hash_it->second.time >= TIME_OUT) {
        available_ports_.push_back(hash_it->second.port);
        flow_hash_.erase(hash_it);
      }
      if (incoming_gate == 1) {
        out_gates[i] = DROP_GATE;  // Drop reverse flow
        continue;
      }

      const auto rule_it = std::find_if(rules_.begin(), rules_.end(),
                                        [&ip](const NATRule &rule) {
                                          return rule.first.Match(ip->src_addr);
                                        });
      if (rule_it == rules_.end()) {
        // No rules found for this source IP address, drop.
        out_gates[i] = DROP_GATE;
        continue;
      }

      uint16_t new_port = available_ports_.back();
      available_ports_.pop_back();
      FlowRecord &record = flow_vec_[new_port - MIN_PORT];

      NAT::Flow ext_flow = flow;
      ext_flow.src_ip = RandomIP(rule_it->second);
      ext_flow.src_port = new_port;

      record.port = new_port;
      record.time = now;
      record.forward_flow = flow;
      record.reverse_flow = ext_flow.reverse_flow();

      flow_hash_.insert({record.forward_flow, record});
      flow_hash_.insert({record.reverse_flow, record});

      ip->src_addr = ext_flow.src_ip;
      ip->dst_addr = ext_flow.dst_ip;
      udp->src_port = ext_flow.src_port;
      udp->dst_port = ext_flow.dst_port;
      compute_cksum(ip, udp);
    }
  }
  RunSplit(out_gates, batch);
}

ADD_MODULE(NAT, "nat", "Network address translator")
