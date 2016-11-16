#ifndef BESS_MODULES_SINK_H_
#define BESS_MODULES_SINK_H_

#include "../module.h"

class Sink : public Module {
 public:
  static const gate_idx_t kNumOGates = 0;

  virtual void ProcessBatch(struct pkt_batch *batch);
};

#endif  // BESS_MODULES_SINK_H_
