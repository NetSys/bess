#ifndef BESS_MODULES_TIMESTAMP_H_
#define BESS_MODULES_TIMESTAMP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class Timestamp final : public Module {
 public:
  using MarkerType = uint32_t;
  static const MarkerType kMarker = 0x54C5BE55;

  pb_error_t Init(const bess::pb::TimestampArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

 private:
  size_t offset_;
};

#endif  // BESS_MODULES_TIMESTAMP_H_
