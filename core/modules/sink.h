#ifndef BESS_MODULES_SINK_H_
#define BESS_MODULES_SINK_H_

#include "../module.h"

class Sink : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 0;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_SINK_H_
