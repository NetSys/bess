#ifndef BESS_MODULES_MERGE_H_
#define BESS_MODULES_MERGE_H_

#include "../module.h"

class Merge final : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;

  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_MERGE_H_
