#include "../module.h"

class NoOP : public Module {
 public:
  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 0;

  virtual struct snobj *Init(struct snobj *arg);

  virtual struct task_result RunTask(void *arg);
};

struct snobj *NoOP::Init(struct snobj *arg) {
  task_id_t tid;

  tid = register_task(this, nullptr);
  if (tid == INVALID_TASK_ID) return snobj_err(ENOMEM, "Task creation failed");

  return nullptr;
}

struct task_result NoOP::RunTask(void *arg) {
  return {};
}

ADD_MODULE(NoOP, "noop", "creates a task that does nothing")
