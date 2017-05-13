#include "macswap.h"

#include "../utils/ether.h"

void MACSwap::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::Ethernet;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    Ethernet *eth = batch->pkts()[i]->head_data<Ethernet *>();
    Ethernet::Address tmp;

    tmp = eth->dst_addr;
    eth->dst_addr = eth->src_addr;
    eth->src_addr = tmp;
  }

  RunNextModule(batch);
}

ADD_MODULE(MACSwap, "macswap", "swaps source/destination MAC addresses")
