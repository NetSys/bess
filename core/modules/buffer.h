#ifndef BESS_MODULES_BUFFER_H_
#define BESS_MODULES_BUFFER_H_

#include "../module.h"

/* TODO: timer-triggered flush */
class Buffer final : public Module {
 public:
  Buffer() : Module(), buf_() {}

  void DeInit() override;

  void ProcessBatch(bess::PacketBatch *batch) override;

 private:
  bess::PacketBatch buf_;
};

#endif  // BESS_MODULES_BUFFER_H_
