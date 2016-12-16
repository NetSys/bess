#include "acl.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lpm.h>
#include <rte_udp.h>

#include <string>

const Commands ACL::cmds = {
    {"add", "ACLArg", MODULE_CMD_FUNC(&ACL::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&ACL::CommandClear), 0}};

pb_error_t ACL::Init(const bess::pb::ACLArg &arg) {
  for (const auto &rule : arg.rules()) {
    ACLRule new_rule = {.src_ip = CIDRNetwork(rule.src_ip()),
                        .dst_ip = CIDRNetwork(rule.dst_ip()),
                        .src_port = static_cast<uint16_t>(rule.src_port()),
                        .dst_port = static_cast<uint16_t>(rule.dst_port()),
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
      if (rule.Match(src_ip, dst_ip, src_port, dst_port)) {
        if (!rule.drop) {
          out_gates[i] = 1 - get_igate();
        }
        break;  // Stop matching other rules
      }
    }
  }
  RunSplit(out_gates, batch);
}
