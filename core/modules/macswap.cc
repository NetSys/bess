#include "macswap.h"

#include <rte_ether.h>

void MACSwap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    char *head = batch->pkts()[i]->head_data<char *>();
    struct ether_hdr *eth = (struct ether_hdr *)head;
    struct ether_addr tmp;

    tmp = eth->d_addr;
    eth->d_addr = eth->s_addr;
    eth->s_addr = tmp;
  }

  RunNextModule(batch);
}

ADD_MODULE(MACSwap, "macswap", "swaps source/destination MAC addresses")
