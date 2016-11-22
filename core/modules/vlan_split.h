#ifndef BESS_MODULES_VLANSPLIT_H_
#define BESS_MODULES_VLANSPLIT_H_

#include "../module.h"

class VLANSplit : public Module {
 public:
  static const gate_idx_t kNumOGates = 4096;

  virtual void ProcessBatch(bess::PacketBatch *batch);
};

#endif  // BESS_MODULES_VLANSPLIT_H_
