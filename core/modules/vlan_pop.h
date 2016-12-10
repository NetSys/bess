#ifndef BESS_MODULES_VLANPOP_H_
#define BESS_MODULES_VLANPOP_H_

#include "../module.h"

class VLANPop final : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_VLANPOP_H_
