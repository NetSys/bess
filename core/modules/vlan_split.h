#ifndef BESS_MODULES_VLANSPLIT_H_
#define BESS_MODULES_VLANSPLIT_H_

#include "../module.h"

class VLANSplit : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 4096;

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_VLANSPLIT_H_
