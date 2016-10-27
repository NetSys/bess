#include "../module.h"

class NoOP : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::protobuf::NoOpArg &arg);

  virtual struct task_result RunTask(void *arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 0;

  static const Commands<Module> cmds;
};

const Commands<Module> NoOP::cmds = {};

struct snobj *NoOP::Init(struct snobj *arg) {
  task_id_t tid;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return snobj_err(ENOMEM, "Task creation failed");

  return nullptr;
}

pb_error_t NoOP::Init(const bess::protobuf::NoOpArg &arg) {
  task_id_t tid;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return pb_error(ENOMEM, "Task creation failed");

  return pb_errno(0);
}

struct task_result NoOP::RunTask(void *arg) {
  return {};
}

ADD_MODULE(NoOP, "noop", "creates a task that does nothing")
