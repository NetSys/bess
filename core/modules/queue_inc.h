#ifndef BESS_MODULES_QUEUEINC_H_
#define BESS_MODULES_QUEUEINC_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../port.h"

class QueueInc final : public Module {
 public:
  static const gate_idx_t kNumIGates = 0;

  static const Commands cmds;

  QueueInc() : Module(), port_(), qid_(), prefetch_(), burst_() {}

  pb_error_t Init(const bess::pb::QueueIncArg &arg);
  void DeInit() override;

  struct task_result RunTask(void *arg) override;

  std::string GetDesc() const override;

  pb_cmd_response_t CommandSetBurst(
      const bess::pb::QueueIncCommandSetBurstArg &arg);

 private:
  Port *port_;
  queue_t qid_;
  int prefetch_;
  int burst_;
  pb_error_t SetBurst(int64_t burst);
};

#endif  // BESS_MODULES_QUEUEINC_H_
