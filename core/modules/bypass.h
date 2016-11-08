#ifndef BESS_MODULES_BYPASS_H_
#define BESS_MODULES_BYPASS_H_

#include "../module.h"

/* This module simply passes packets from input gate X down to output gate X
 * (the same gate index) */

class Bypass : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_BYPASS_H_
