#ifndef BESS_MODULES_QUEUE_H_
#define BESS_MODULES_QUEUE_H_

#include "../kmod/llring.h"
#include "../module.h"
#include "../module_msg.pb.h"

class Queue final : public Module {
 public:
  static const Commands cmds;

  Queue() : Module(), queue_(), prefetch_(), burst_() {
    propagate_workers_ = false;
  }

  CommandResponse Init(const bess::pb::QueueArg &arg);

  void DeInit() override;

  struct task_result RunTask(void *arg) override;
  void ProcessBatch(bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

  CommandResponse CommandSetBurst(const bess::pb::QueueCommandSetBurstArg &arg);
  CommandResponse CommandSetSize(const bess::pb::QueueCommandSetSizeArg &arg);

  CheckConstraintResult CheckModuleConstraints() const override;

 private:
  int Resize(int slots);
  CommandResponse SetSize(uint64_t size);

  struct llring *queue_;
  bool prefetch_;
  int burst_;
};

#endif  // BESS_MODULES_QUEUE_H_
