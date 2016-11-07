#ifndef __VLAN_POP_H__
#define __VLAN_POP_H__

#include "../module.h"

class VLANPop : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif
