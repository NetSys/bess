#ifndef __IP_ENCAP_H__
#define __IP_ENCAP_H__

#include "../module.h"

class IPEncap : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  int num_attrs = 5;
  struct mt_attr attrs[MAX_ATTRS_PER_MODULE] = {
      {
          .name = "ip_src", .size = 4, .mode = MT_READ,
      },
      {
          .name = "ip_dst", .size = 4, .mode = MT_READ,
      },
      {
          .name = "ip_proto", .size = 1, .mode = MT_READ,
      },
      {
          .name = "ip_nexthop", .size = 4, .mode = MT_WRITE,
      },
      {
          .name = "ether_type", .size = 2, .mode = MT_WRITE,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
};

#endif
