#ifndef __ETHERENCAP_H__
#define __ETHERENCAP_H__

#include "../module.h"

class EtherEncap : public Module {
 public:
  void ProcessBatch(struct pkt_batch *batch);

  size_t num_attrs = 5;
  struct bess::metadata::mt_attr attrs[bess::metadata::kMaxAttrsPerModule] = {
      {
          .name = "ether_src",
          .size = ETHER_ADDR_LEN,
          .mode = bess::metadata::AccessMode::READ,
      },
      {
          .name = "ether_dst",
          .size = ETHER_ADDR_LEN,
          .mode = bess::metadata::AccessMode::READ,
      },
      {
          .name = "ether_type", .size = 2, .mode = bess::metadata::AccessMode::READ,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
};

#endif
