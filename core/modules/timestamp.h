#ifndef BESS_MODULES_TIMESTAMP_H_
#define BESS_MODULES_TIMESTAMP_H_

#include "../module.h"

class Timestamp final : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_TIMESTAMP_H_
