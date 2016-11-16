#ifndef BESS_MODULES_MACSWAP_H_
#define BESS_MODULES_MACSWAP_H_

#include "../module.h"

class MACSwap : public Module {
 public:
  virtual void ProcessBatch(struct bess::pkt_batch *batch);
};

#endif  // BESS_MODULES_MACSWAP_H_
