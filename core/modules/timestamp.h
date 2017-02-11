#ifndef BESS_MODULES_TIMESTAMP_H_
#define BESS_MODULES_TIMESTAMP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class Timestamp final : public Module {
 public:
  pb_error_t Init(const bess::pb::TimestampArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

 private:
  size_t offset_;
};

#endif  // BESS_MODULES_TIMESTAMP_H_
