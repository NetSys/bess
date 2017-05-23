#ifndef BESS_MODULES_IP_CHECKSUM_H_
#define BESS_MODULES_IP_CHECKSUM_H_

#include "../module.h"

// Compute IP checksum on packet
class IPChecksum final : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_IP_CHECKSUM_H_
