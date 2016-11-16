#ifndef BESS_MODULES_VLANPOP_H_
#define BESS_MODULES_VLANPOP_H_

#include "../module.h"

class VLANPop : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);
};

#endif  // BESS_MODULES_VLANPOP_H_
