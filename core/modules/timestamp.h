#ifndef BESS_MODULES_TIMESTAMP_H_
#define BESS_MODULES_TIMESTAMP_H_

#include "../module.h"

class Timestamp final : public Module {
 public:
  virtual void ProcessBatch(bess::PacketBatch *batch);
};

#endif  // BESS_MODULES_TIMESTAMP_H_
