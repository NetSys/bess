#ifndef __MERGE_H__
#define __MERGE_H__

#include "../module.h"

class Merge : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = 1;

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const Commands<Module> cmds;
};

#endif
