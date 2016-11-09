#include "track.h"

void TrackGate::ProcessBatch(struct pkt_batch *batch) {
  cnt_ += 1;
  pkts_ += batch->cnt;
}
