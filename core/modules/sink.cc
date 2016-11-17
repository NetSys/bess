#include "sink.h"

void Sink::ProcessBatch(bess::PacketBatch *batch) {
  bess::Packet::Free(batch);
}

ADD_MODULE(Sink, "sink", "discards all packets")
