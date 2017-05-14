#include "acl.h"

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/udp.h"

const Commands ACL::cmds = {
    {"add", "ACLArg", MODULE_CMD_FUNC(&ACL::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&ACL::CommandClear), 0}};

CommandResponse ACL::Init(const bess::pb::ACLArg &arg) {
  for (const auto &rule : arg.rules()) {
    ACLRule new_rule = {
        .src_ip = Ipv4Prefix(rule.src_ip()),
        .dst_ip = Ipv4Prefix(rule.dst_ip()),
        .src_port = be16_t(static_cast<uint16_t>(rule.src_port())),
        .dst_port = be16_t(static_cast<uint16_t>(rule.dst_port())),
        .drop = rule.drop()};
    rules_.push_back(new_rule);
  }
  return CommandSuccess();
}

CommandResponse ACL::CommandAdd(const bess::pb::ACLArg &arg) {
  Init(arg);
  return CommandSuccess();
}

CommandResponse ACL::CommandClear(const bess::pb::EmptyArg &) {
  rules_.clear();
  return CommandSuccess();
}

void ACL::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;

  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  gate_idx_t incoming_gate = get_igate();

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
    size_t ip_bytes = ip->header_length << 2;
    Udp *udp =
        reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);

    out_gates[i] = DROP_GATE;  // By default, drop unmatched packets

    for (const auto &rule : rules_) {
      if (rule.Match(ip->src, ip->dst, udp->src_port, udp->dst_port)) {
        if (!rule.drop) {
          out_gates[i] = incoming_gate;
        }
        break;  // Stop matching other rules
      }
    }
  }
  RunSplit(out_gates, batch);
}

ADD_MODULE(ACL, "acl", "ACL module from NetBricks")
