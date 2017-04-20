#ifndef BESS_MODULES_WORKERSPLIT_H_
#define BESS_MODULES_WORKERSPLIT_H_

#include "../module.h"

class WorkerSplit final : public Module {
 public:
  static const gate_idx_t kNumOGates = Worker::kMaxWorkers;

  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_WORKERSPLIT_H_
