#include "sink.h"

void Sink::ProcessBatch(struct bess::pkt_batch *batch) {
  bess::Packet::free_bulk(batch->pkts, batch->cnt);
}

ADD_MODULE(Sink, "sink", "discards all packets")
