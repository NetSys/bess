#ifndef BESS_MODULES_IPENCAP_H_
#define BESS_MODULES_IPENCAP_H_

#include "../module.h"

class IPEncap : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  size_t num_attrs = 5;
  struct bess::metadata::mt_attr attrs[bess::metadata::kMaxAttrsPerModule] = {
      {
          .name = "ip_src", .size = 4, .mode = bess::metadata::AccessMode::READ,
      },
      {
          .name = "ip_dst", .size = 4, .mode = bess::metadata::AccessMode::READ,
      },
      {
          .name = "ip_proto",
          .size = 1,
          .mode = bess::metadata::AccessMode::READ,
      },
      {
          .name = "ip_nexthop",
          .size = 4,
          .mode = bess::metadata::AccessMode::WRITE,
      },
      {
          .name = "ether_type",
          .size = 2,
          .mode = bess::metadata::AccessMode::WRITE,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_IPENCAP_H_
