#ifndef BESS_MODULES_TIMESTAMP_H_
#define BESS_MODULES_TIMESTAMP_H_

#include "../module.h"

class Timestamp : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);
};

#endif  // BESS_MODULES_TIMESTAMP_H_
