#ifndef BESS_MODULES_NOOP_H_
#define BESS_MODULES_NOOP_H_

#include "../module.h"

class NoOP : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const google::protobuf::Any &arg);

  virtual struct task_result RunTask(void *arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 0;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;
};

#endif  // BESS_MODULES_NOOP_H_
