#include "acl.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lpm.h>
#include <rte_udp.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <string>

const Commands ACL::cmds = {
    {"add", "ACLArg", MODULE_CMD_FUNC(&ACL::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&ACL::CommandClear), 0}};

inline static CIDRNetwork CreateNetwork(const std::string &cidr) {
  if (cidr.length() == 0) {
    return std::make_pair(0, 0);
  }

  size_t delim_pos = cidr.find('/');
  DCHECK_NE(delim_pos, std::string::npos);
  DCHECK_LT(delim_pos, cidr.length());

  IPAddress ip_addr;
  inet_pton(AF_INET, cidr.substr(0, delim_pos).c_str(), &ip_addr);

  const int len = std::stoi(cidr.substr(delim_pos + 1));
  IPAddress mask = ~((1 << len) - 1);

  return std::make_pair(ntohl(ip_addr), mask);
}

// ip should be in host order
inline static bool match_cidr(const CIDRNetwork &cidr, const IPAddress ip) {
  return (cidr.first & cidr.second) == (ip & cidr.second);
}

// src_ip, dst_ip, src_port, dst_port should be in host order
inline static bool match(const ACL::ACLRule &rule, IPAddress src_ip,
                         IPAddress dst_ip, uint16_t src_port,
                         uint16_t dst_port) {
  return ((rule.src_ip.first != 0 && match_cidr(rule.src_ip, src_ip)) &&
          (rule.dst_ip.first != 0 && match_cidr(rule.dst_ip, dst_ip)) &&
          (rule.src_port != 0 && rule.src_port == src_port) &&
          (rule.dst_port != 0 && rule.dst_port == dst_port));
}

pb_error_t ACL::Init(const bess::pb::ACLArg &arg) {
  for (const auto &rule : arg.rules()) {
    CIDRNetwork src_ip = CreateNetwork(rule.src_ip());
    CIDRNetwork dst_ip = CreateNetwork(rule.dst_ip());
    uint16_t src_port = rule.src_port();
    uint16_t dst_port = rule.dst_port();
    ACLRule new_rule = {.src_ip = src_ip,
                        .dst_ip = dst_ip,
                        .src_port = src_port,
                        .dst_port = dst_port,
                        .established = rule.established(),
                        .drop = rule.drop()};
    rules_.push_back(new_rule);
  }
  return pb_errno(0);
}

pb_cmd_response_t ACL::CommandAdd(const bess::pb::ACLArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, Init(arg));
  return response;
}

pb_cmd_response_t ACL::CommandClear(const bess::pb::EmptyArg &) {
  rules_.clear();
  return pb_cmd_response_t();
}

void ACL::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct ether_hdr *eth = pkt->head_data<struct ether_hdr *>();
    struct ipv4_hdr *ip = reinterpret_cast<struct ipv4_hdr *>(eth + 1);
    struct udp_hdr *udp = reinterpret_cast<struct udp_hdr *>(ip + 1);

    IPAddress src_ip = ntohl(ip->src_addr);
    IPAddress dst_ip = ntohl(ip->dst_addr);
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dst_port = ntohs(udp->dst_port);

    out_gates[i] = DROP_GATE;  // By default, drop unmatched packets

    for (const auto &rule : rules_) {
      if (match(rule, src_ip, dst_ip, src_port, dst_port)) {
        if (!rule.drop) {
          out_gates[i] = 1 - get_igate();
        }
        break;  // Stop matching other rules
      }
    }
  }
  RunSplit(out_gates, batch);
}
