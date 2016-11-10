#ifndef BESS_MODULES_IPENCAP_H_
#define BESS_MODULES_IPENCAP_H_

#include "../module.h"

using bess::metadata::Attribute;

class IPEncap : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  size_t num_attrs = 5;
  struct Attribute attrs[bess::metadata::kMaxAttrsPerModule] = {
      {
          .name = "ip_src", .size = 4, .mode = Attribute::AccessMode::kRead,
      },
      {
          .name = "ip_dst", .size = 4, .mode = Attribute::AccessMode::kRead,
      },
      {
          .name = "ip_proto",
          .size = 1,
          .mode = Attribute::AccessMode::kRead,
      },
      {
          .name = "ip_nexthop",
          .size = 4,
          .mode = Attribute::AccessMode::kRead,
      },
      {
          .name = "ether_type",
          .size = 2,
          .mode = Attribute::AccessMode::kWrite,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_IPENCAP_H_
