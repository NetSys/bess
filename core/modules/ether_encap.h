#ifndef __ETHERENCAP_H__
#define __ETHERENCAP_H__

#include "../module.h"

class EtherEncap : public Module {
 public:
  void ProcessBatch(struct pkt_batch *batch);

  int num_attrs = 5;
  struct mt_attr attrs[MAX_ATTRS_PER_MODULE] = {
      {
          .name = "ether_src", .size = ETHER_ADDR_LEN, .mode = MT_READ,
      },
      {
          .name = "ether_dst", .size = ETHER_ADDR_LEN, .mode = MT_READ,
      },
      {
          .name = "ether_type", .size = 2, .mode = MT_READ,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
};

#endif
