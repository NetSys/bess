#ifndef BESS_MODULES_L4_CHECKSUM_H_
#define BESS_MODULES_L4_CHECKSUM_H_

#include "../module.h"

// Compute L4 checksum on packet
class L4Checksum final : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_L4_CHECKSUM_H_
