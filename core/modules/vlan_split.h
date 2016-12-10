#ifndef BESS_MODULES_VLANSPLIT_H_
#define BESS_MODULES_VLANSPLIT_H_

#include "../module.h"

class VLANSplit final : public Module {
 public:
  static const gate_idx_t kNumOGates = 4096;

  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_VLANSPLIT_H_
