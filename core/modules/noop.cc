#include "noop.h"

CommandResponse NoOP::Init(const bess::pb::EmptyArg &) {
  task_id_t tid;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return CommandFailure(ENOMEM, "Task creation failed");

  return CommandSuccess();
}

struct task_result NoOP::RunTask(void *) {
  return {.block = false, .packets = 0, .bits = 0};
}

ADD_MODULE(NoOP, "noop", "creates a task that does nothing")
