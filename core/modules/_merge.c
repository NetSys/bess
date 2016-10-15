#include "../module.h"

class Merge : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = 1;

  static const std::vector<struct Command> cmds;
};

const std::vector<struct Command> Merge::cmds = {};

void Merge::ProcessBatch(struct pkt_batch *batch) {
  run_next_module(this, batch);
}

ADD_MODULE(Merge, "merge", "All input gates go out of a single output gate")
