#ifndef BESS_MODULES_UPDATE_TTL_H_
#define BESS_MODULES_UPDATE_TTL_H_

#include "../module.h"

// Updates TTl of packets by decrementing by 1 and dropping packets if their TTl <= 1
class UpdateTTL final : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_UPDATE_TTL_H_
