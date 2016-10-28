#ifndef __IP_ENCAP_H__
#define __IP_ENCAP_H__

#include "../module.h"

class IPEncap : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  int num_attrs = 5;
  struct bess::metadata::mt_attr attrs[bess::metadata::kMaxAttrsPerModule] = {
      {
          .name = "ip_src", .size = 4, .mode = bess::metadata::MT_READ,
      },
      {
          .name = "ip_dst", .size = 4, .mode = bess::metadata::MT_READ,
      },
      {
          .name = "ip_proto", .size = 1, .mode = bess::metadata::MT_READ,
      },
      {
          .name = "ip_nexthop", .size = 4, .mode = bess::metadata::MT_WRITE,
      },
      {
          .name = "ether_type", .size = 2, .mode = bess::metadata::MT_WRITE,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
};

#endif
