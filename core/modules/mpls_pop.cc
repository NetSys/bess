#include "mpls_pop.h"

#include "../utils/ether.h"
#include "../utils/mpls.h"


void MPLSPop::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::be16_t;
  using bess::utils::Ethernet;
  using bess::utils::Mpls;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();

    if (eth->ether_type != be16_t(Ethernet::Type::kMpls)) {
      // Currently ignore non MPLS packets
      continue;
    }

    // TODO(gsagie) save the MPLS label as metadata
    // Mpls *mpls = reinterpret_cast<Mpls *>(eth_header + 1);

    // TODO(gsagie) convert this to be more efficient using Intel instructions
    Ethernet::Address src_addr = eth->src_addr;
    Ethernet::Address dst_addr = eth->dst_addr;

    pkt->adj(4);
    Ethernet *eth_new = pkt->head_data<Ethernet *>();
    eth_new->src_addr = src_addr;
    eth_new->dst_addr = dst_addr;
    eth_new->ether_type = be16_t(Ethernet::Type::kIpv4);
  }

  RunNextModule(batch);
}

ADD_MODULE(MPLSPop, "mpls_pop", "Pop MPLS label")
