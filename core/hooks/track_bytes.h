#ifndef BESS_HOOKS_TRACK_BYTES_
#define BESS_HOOKS_TRACK_BYTES_

#include "../module.h"

const std::string kGateHookTrackBytes = "track_bytes";
const uint16_t kGateHookPriorityTrackBytes = 2;

// TrackBytes counts the number of bytes seen by a gate.
class TrackBytes final : public bess::GateHook {
 public:
  TrackBytes()
      : bess::GateHook(kGateHookTrackBytes, kGateHookPriorityTrackBytes),
        bytes_(){};

  uint64_t bytes() const { return bytes_; }

  void ProcessBatch(const bess::PacketBatch *batch);

 private:
  uint64_t bytes_;
};

#endif  // BESS_HOOKS_TRACK_BYTES_
