#include "sink.h"

const Commands<Module> Sink::cmds = {};

void Sink::ProcessBatch(struct pkt_batch *batch) {
  snb_free_bulk(batch->pkts, batch->cnt);
}

ADD_MODULE(Sink, "sink", "discards all packets")
