#include "../module.h"

/* This module simply passes packets from input gate X down to output gate X
 * (the same gate index) */

class Bypass : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
};

const Commands<Module> Bypass::cmds = {};

void Bypass::ProcessBatch(struct pkt_batch *batch) {
  RunChooseModule(get_igate(), batch);
}

ADD_MODULE(Bypass, "bypass", "bypasses packets without any processing")
