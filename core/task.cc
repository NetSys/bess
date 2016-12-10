#include "task.h"

#include <glog/logging.h>

#include <cassert>

#include "mem_alloc.h"
#include "module.h"
#include "opts.h"
#include "traffic_class.h"
#include "worker.h"

Task::Task(Module *m, void *arg, bess::LeafTrafficClass *c)
    : m_(m), arg_(arg), c_(c) {
  CHECK(c) << "Tasks must always be attached to a leaf traffic class.";
  if (c_) {
    c_->AddTask(this);
  }
}

Task::~Task() {
  if (c_) {
    c_->RemoveTask(this);
  }
}

void Task::Attach(bess::LeafTrafficClass *c) {
  if (c_) {
    c_->RemoveTask(this);
  }
  c_ = c;
  c_->AddTask(this);
}

struct task_result Task::Scheduled() {
  struct task_result ret = m_->RunTask(arg_);
  return ret;
}
