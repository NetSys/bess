#include "track.h"

#include "../message.h"

// Ethernet overhead in bytes
static const size_t kEthernetOverhead = 24;

const std::string TrackGate::kName = "track";

TrackGate::TrackGate()
    : bess::GateHook(TrackGate::kName, TrackGate::kPriority),
      track_bytes_(),
      cnt_(),
      pkts_() {}

CommandResponse TrackGate::Init(const bess::Gate *,
                                const bess::pb::TrackModuleRequest &arg) {
  track_bytes_ = arg.bits();
  return CommandSuccess();
}

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

ADD_GATE_HOOK(TrackGate)
