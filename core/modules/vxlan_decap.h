#ifndef BESS_MODULES_VXLANDECAP_H_
#define BESS_MODULES_VXLANDECAP_H_

#include "../module.h"

class VXLANDecap : public Module {
 public:
  void ProcessBatch(struct pkt_batch *batch);

  size_t num_attrs = 3;
  struct bess::metadata::Attribute attrs[bess::metadata::kMaxAttrsPerModule] = {
      {
          .name = "tun_ip_src",
          .size = 4,
          .mode = bess::metadata::Attribute::AccessMode::kWrite,
      },
      {
          .name = "tun_ip_dst",
          .size = 4,
          .mode = bess::metadata::Attribute::AccessMode::kWrite,
      },
      {
          .name = "tun_id",
          .size = 4,
          .mode = bess::metadata::Attribute::AccessMode::kWrite,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_VXLANDECAP_H_
