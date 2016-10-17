#include "../module.h"

class Sink : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 0;

  static const std::vector<struct Command> cmds;
};

void Sink::ProcessBatch(struct pkt_batch *batch) {
  snb_free_bulk(batch->pkts, batch->cnt);
}

const std::vector<struct Command> Sink::cmds = {};

ADD_MODULE(Sink, "sink", "discards all packets")
