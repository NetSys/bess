#ifndef BESS_HOOKS_TRACK_
#define BESS_HOOKS_TRACK_

#include "../message.h"
#include "../module.h"

// TrackGate counts the number of packets, batches and bytes seen by a gate.
class Track final : public bess::GateHook {
 public:
  Track();

  CommandResponse Init(const bess::Gate *, const bess::pb::TrackArg &);

  uint64_t cnt() const { return cnt_; }

  uint64_t pkts() const { return pkts_; }

  uint64_t bytes() const { return bytes_; }

  void set_track_bytes(bool track) { track_bytes_ = track; }

  void ProcessBatch(const bess::PacketBatch *batch);

  static constexpr uint16_t kPriority = 0;
  static const std::string kName;

 private:
  bool track_bytes_;
  uint64_t cnt_;
  uint64_t pkts_;
  uint64_t bytes_;
};

#endif  // BESS_HOOKS_TRACK_
