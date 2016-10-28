#include "noop.h"

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
