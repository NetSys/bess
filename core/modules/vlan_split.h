#ifndef __VLAN_SPLIT_H__
#define __VLAN_SPLIT_H__

#include "../module.h"

class VLANSplit : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 4096;

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const Commands<Module> cmds;
};

#endif
