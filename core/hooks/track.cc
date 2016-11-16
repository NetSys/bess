#include "track.h"

void TrackGate::ProcessBatch(const struct bess::pkt_batch *batch) {
  cnt_ += 1;
  pkts_ += batch->cnt;
}
