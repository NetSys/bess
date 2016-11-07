#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "../module.h"

/* TODO: timer-triggered flush */
class Buffer : public Module {
 public:
  Buffer() : Module(), buf_() {}

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  struct pkt_batch buf_;
};

#endif
