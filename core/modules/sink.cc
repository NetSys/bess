#include "sink.h"

void Sink::ProcessBatch(struct pkt_batch *batch) {
  snb_free_bulk(batch->pkts, batch->cnt);
}

ADD_MODULE(Sink, "sink", "discards all packets")
