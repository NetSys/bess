#ifndef BESS_MODULES_MERGE_H_
#define BESS_MODULES_MERGE_H_

#include "../module.h"

class Merge : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;

  virtual void ProcessBatch(struct pkt_batch *batch);
};

#endif  // BESS_MODULES_MERGE_H_
