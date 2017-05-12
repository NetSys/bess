#include "macswap.h"

#include "../utils/ether.h"

void MACSwap::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::EthHeader;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    EthHeader *eth = batch->pkts()[i]->head_data<EthHeader *>();
    EthHeader::Address tmp;

    tmp = eth->dst_addr;
    eth->dst_addr = eth->src_addr;
    eth->src_addr = tmp;
  }

  RunNextModule(batch);
}

ADD_MODULE(MACSwap, "macswap", "swaps source/destination MAC addresses")
