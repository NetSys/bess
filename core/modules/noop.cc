#include "noop.h"

pb_error_t NoOP::Init(const bess::pb::EmptyArg &) {
  task_id_t tid;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return pb_error(ENOMEM, "Task creation failed");

  return pb_errno(0);
}

struct task_result NoOP::RunTask(void *) {
  return {
      .packets = 0, .bits = 0,
  };
}

ADD_MODULE(NoOP, "noop", "creates a task that does nothing")
