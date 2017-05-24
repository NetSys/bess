#include "track_bytes.h"

void TrackBytes::ProcessBatch(const bess::PacketBatch *batch) {
  size_t cnt = batch->cnt();
  for (size_t i = 0; i < cnt; i++) {
    bytes_ += batch->pkts()[i]->data_len();
  }
}
