#ifndef BESS_MODULES_NOOP_H_
#define BESS_MODULES_NOOP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class NoOP final : public Module {
 public:
  CommandResponse Init(const bess::pb::EmptyArg &arg);

  struct task_result RunTask(void *arg) override;

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 0;
};

#endif  // BESS_MODULES_NOOP_H_
