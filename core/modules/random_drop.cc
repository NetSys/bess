#include "random_drop.h"
#include <string>
#include <time.h>

CommandResponse RandomDrop::Init(const bess::pb::RandomDropArg &arg) {
  double drop_rate = arg.drop_rate();
  if (drop_rate < 0 || drop_rate > 1) {
    return CommandFailure(EINVAL, "drop rate needs to be between [0, 1]");
  }
  threshold_ = drop_rate * kRange;
  return CommandSuccess();
}

void RandomDrop::ProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch out_batch;
  bess::PacketBatch free_batch;
  out_batch.clear();
  free_batch.clear();

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    if (rng_.GetRange(kRange) > threshold_) {
      out_batch.add(pkt);
    } else {
      free_batch.add(pkt);
    }
  }

  bess::Packet::Free(&free_batch);
  RunNextModule(&out_batch);
}

ADD_MODULE(RandomDrop, "random_drop", "randomly drops packets")
