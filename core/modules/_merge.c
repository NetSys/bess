#include "../module.h"

class Merge : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = 1;
};

void Merge::ProcessBatch(struct pkt_batch *batch) {
  run_next_module(this, batch);
}

ModuleClassRegister<Merge> merge(
    "Merge", "merge", "All input gates go out of a single output gate");
