#ifndef BESS_MODULES_QUEUE_H_
#define BESS_MODULES_QUEUE_H_

#include "../kmod/llring.h"
#include "../module.h"
#include "../module_msg.pb.h"

class Queue final : public Module {
 public:
  static const Commands cmds;

  Queue() : Module(), queue_(), prefetch_(), burst_() {
    is_task_ = true;
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
  const double kHighWaterRatio = 0.90;
  const double kLowWaterRatio = 0.15;

  int Resize(int slots);

  // Readjusts the water level according to `size_`.
  void AdjustWaterLevels();

  CommandResponse SetSize(uint64_t size);

  struct llring *queue_;
  bool prefetch_;

  // Whether backpressure should be applied or not
  bool backpressure_;

  int burst_;

  // Queue capacity
  uint64_t size_;

  // High water occupancy
  uint64_t high_water_;

  // Low water occupancy
  uint64_t low_water_;
};

#endif  // BESS_MODULES_QUEUE_H_
