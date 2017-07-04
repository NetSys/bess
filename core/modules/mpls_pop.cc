#include "mpls_pop.h"

#include "../utils/ether.h"
#include "../utils/mpls.h"

using bess::utils::Ethernet;
using bess::utils::Mpls;

const Commands MPLSPop::cmds = {
    {"set", "MplsPopArg", MODULE_CMD_FUNC(&MPLSPop::CommandSet), 0}};

// TODO(gsagie) make the next eth type and remove eth header
//              configurable per MPLS label (with default)
MPLSPop::MPLSPop()
    : next_ether_type_(be16_t(Ethernet::Type::kIpv4)),
      remove_eth_header_(false) {}

void MPLSPop::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();

    if (eth->ether_type != be16_t(Ethernet::Type::kMpls)) {
      // non MPLS packets are sent to different output gate
      out_gates[i] = 1;
      continue;
    }
    out_gates[i] = 0;

    // TODO(gsagie) save the MPLS label as metadata
    // Mpls *mpls = reinterpret_cast<Mpls *>(eth_header + 1);

    // TODO(gsagie) convert this to be more efficient using Intel instructions
    if (remove_eth_header_) {
      pkt->adj(sizeof(Ethernet) + sizeof(Mpls));
    } else {
      Ethernet::Address src_addr = eth->src_addr;
      Ethernet::Address dst_addr = eth->dst_addr;

      pkt->adj(sizeof(Mpls));
      Ethernet *eth_new = pkt->head_data<Ethernet *>();
      eth_new->src_addr = src_addr;
      eth_new->dst_addr = dst_addr;
      eth_new->ether_type = next_ether_type_;
    }
  }

  RunSplit(out_gates, batch);
}

CommandResponse MPLSPop::CommandSet(const bess::pb::MplsPopArg &arg) {
  remove_eth_header_ = arg.remove_eth_header();
  next_ether_type_ = be16_t(arg.next_eth_type());
  return CommandSuccess();
}

ADD_MODULE(MPLSPop, "mpls_pop", "Pop MPLS label")
