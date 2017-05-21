#include "track.h"

// Ethernet overhead in bytes
static const size_t kEthernetOverhead = 24;

void TrackGate::ProcessBatch(const bess::PacketBatch *batch) {
  size_t cnt = batch->cnt();
  cnt_ += 1;
  pkts_ += cnt;

  if (!track_bytes_) {
    return;
  }

  for (size_t i = 0; i < cnt; i++) {
    bytes_ += batch->pkts()[i]->data_len() + kEthernetOverhead;
  }
}
