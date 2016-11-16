#include "noop.h"

struct snobj *NoOP::Init(struct snobj *) {
  task_id_t tid;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return snobj_err(ENOMEM, "Task creation failed");

  return nullptr;
}

pb_error_t NoOP::InitPb(const bess::pb::EmptyArg &) {
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
