#include "../module.h"

#include <rte_ether.h>

class MACSwap : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const std::vector<struct Command> cmds;
};

const std::vector<struct Command> MACSwap::cmds = {};

void MACSwap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    char *head = static_cast<char *>(snb_head_data(batch->pkts[i]));
    struct ether_hdr *eth = (struct ether_hdr *)head;
    struct ether_addr tmp;

    tmp = eth->d_addr;
    eth->d_addr = eth->s_addr;
    eth->s_addr = tmp;
  }

  run_next_module(this, batch);
}

ADD_MODULE(MACSwap, "macswap", "swaps source/destination MAC addresses")
