#ifndef BESS_MODULES_MERGE_H_
#define BESS_MODULES_MERGE_H_

#include "../module.h"

class Merge : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = 1;

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_MERGE_H_
