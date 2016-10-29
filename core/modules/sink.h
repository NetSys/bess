#ifndef __SINK_H__
#define __SINK_H__

#include "../module.h"

class Sink : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 0;

  static const Commands<Module> cmds;
};

#endif
