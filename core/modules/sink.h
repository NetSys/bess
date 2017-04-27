#ifndef BESS_MODULES_SINK_H_
#define BESS_MODULES_SINK_H_

#include "../module.h"

class Sink final : public Module {
 public:
  Sink() { max_allowed_workers_ = Worker::kMaxWorkers; }

  static const gate_idx_t kNumOGates = 0;

  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_SINK_H_
