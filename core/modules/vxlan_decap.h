#ifndef __VXLAN_DECAP_H__
#define __VXLAN_DECAP_H__

#include "../module.h"

class VXLANDecap : public Module {
 public:
  void ProcessBatch(struct pkt_batch *batch);

  size_t num_attrs = 3;
  struct bess::metadata::mt_attr attrs[bess::metadata::kMaxAttrsPerModule] = {
      {
          .name = "tun_ip_src", .size = 4, .mode = bess::metadata::AccessMode::WRITE,
      },
      {
          .name = "tun_ip_dst", .size = 4, .mode = bess::metadata::AccessMode::WRITE,
      },
      {
          .name = "tun_id", .size = 4, .mode = bess::metadata::AccessMode::WRITE,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
};

#endif
