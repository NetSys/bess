#ifndef BESS_MODULES_VLANPOP_H_
#define BESS_MODULES_VLANPOP_H_

#include "../module.h"

class VLANPop final : public Module {
 public:
  virtual void ProcessBatch(bess::PacketBatch *batch);
};

#endif  // BESS_MODULES_VLANPOP_H_
