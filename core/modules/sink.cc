#include "../module.h"

class Sink : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 0;

  virtual void ProcessBatch(struct pkt_batch *batch);
};

void Sink::ProcessBatch(struct pkt_batch *batch) {
  snb_free_bulk(batch->pkts, batch->cnt);
}

ADD_MODULE(Sink, "sink", "discards all packets")
