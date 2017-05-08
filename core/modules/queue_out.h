#ifndef BESS_MODULES_QUEUEOUT_H_
#define BESS_MODULES_QUEUEOUT_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../port.h"

class QueueOut final : public Module {
 public:
  static const gate_idx_t kNumOGates = 0;

  QueueOut() : Module(), port_(), qid_() {}

  CommandResponse Init(const bess::pb::QueueOutArg &arg);

  void DeInit() override;

  void ProcessBatch(bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

 private:
  Port *port_;
  queue_t qid_;
};

#endif  // BESS_MODULES_QUEUEOUT_H_
