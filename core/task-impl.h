#ifndef BESS_TASK_IMPL_H_
#define BESS_TASK_IMPL_H_

#include "task.h"

#include "module.h"

inline struct task_result Task::operator()(void)
{
  return module_->RunTask(arg_);
}

#endif  // BESS_TASK_IMPL_H_
