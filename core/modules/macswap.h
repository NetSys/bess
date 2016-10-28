#ifndef __MACSWAP_H__
#define __MACSWAP_H__

#include "../module.h"

class MACSwap : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
};

#endif
