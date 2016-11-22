#include "track.h"

void TrackGate::ProcessBatch(const bess::PacketBatch *batch) {
  cnt_ += 1;
  pkts_ += batch->cnt();
}
