#ifndef BESS_MODULES_MPLSPOP_H_
#define BESS_MODULES_MPLSPOP_H_

#include "../module.h"

class MPLSPop final : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_MPLSPOP_H_
