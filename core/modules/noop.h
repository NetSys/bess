#ifndef BESS_MODULES_NOOP_H_
#define BESS_MODULES_NOOP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class NoOP : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::EmptyArg &arg);

  virtual struct task_result RunTask(void *arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 0;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_NOOP_H_
