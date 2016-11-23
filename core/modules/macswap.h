#ifndef BESS_MODULES_MACSWAP_H_
#define BESS_MODULES_MACSWAP_H_

#include "../module.h"

class MACSwap final : public Module {
 public:
  virtual void ProcessBatch(bess::PacketBatch *batch);
};

#endif  // BESS_MODULES_MACSWAP_H_
