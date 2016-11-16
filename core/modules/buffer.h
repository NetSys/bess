#ifndef BESS_MODULES_BUFFER_H_
#define BESS_MODULES_BUFFER_H_

#include "../module.h"

/* TODO: timer-triggered flush */
class Buffer : public Module {
 public:
  Buffer() : Module(), buf_() {}

  virtual void Deinit();

  virtual void ProcessBatch(struct bess::pkt_batch *batch);

 private:
  struct bess::pkt_batch buf_;
};

#endif  // BESS_MODULES_BUFFER_H_
