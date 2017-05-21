#ifndef BESS_HOOKS_TRACK_
#define BESS_HOOKS_TRACK_

#include "../module.h"

const std::string kGateHookTrackGate = "track_gate";
const uint16_t kGateHookPriorityTrackGate = 0;

// TrackGate counts the number of packets, batches and bytes seen by a gate.
class TrackGate final : public bess::GateHook {
 public:
  TrackGate(bool track_bytes = false)
      : bess::GateHook(kGateHookTrackGate, kGateHookPriorityTrackGate),
        track_bytes_(track_bytes),
        cnt_(),
        pkts_(){};

  uint64_t cnt() const { return cnt_; }

  uint64_t pkts() const { return pkts_; }

  uint64_t bytes() const { return bytes_; }

  void set_track_bytes(bool track) { track_bytes_ = track; }

  void ProcessBatch(const bess::PacketBatch *batch);

 private:
  bool track_bytes_;
  uint64_t cnt_;
  uint64_t pkts_;
  uint64_t bytes_;
};

#endif  // BESS_HOOKS_TRACK_
