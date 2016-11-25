#ifndef BESS_HOOKS_TRACK_
#define BESS_HOOKS_TRACK_

#include "../module.h"

const std::string kGateHookTrackGate = "track_gate";
const uint16_t kGateHookPriorityTrackGate = 0;

// TrackGate counts the number of packets and batches seen by a gate.
class TrackGate final : public bess::GateHook {
 public:
  TrackGate()
      : bess::GateHook(kGateHookTrackGate, kGateHookPriorityTrackGate),
        cnt_(),
        pkts_(){};

  uint64_t cnt() const { return cnt_; }
  void incr_cnt(uint64_t n) { cnt_ += n; }

  uint64_t pkts() const { return pkts_; }
  void incr_pkts(uint64_t n) { pkts_ += n; }

  void ProcessBatch(const bess::PacketBatch *batch);

 private:
  uint64_t cnt_;
  uint64_t pkts_;
};

#endif  // BESS_HOOKS_TRACK_
