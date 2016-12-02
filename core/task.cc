#include "task.h"

#include <cassert>

#include <glog/logging.h>

#include "mem_alloc.h"
#include "module.h"
#include "opts.h"
#include "traffic_class.h"
#include "worker.h"

Task::Task(Module *m, void *arg, bess::LeafTrafficClass *c) : m_(m), arg_(arg), c_(c) {
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

  // Depth first goes through all pending modules and services
  while (ctx.gates_pending()) {
    struct gate_task task = ctx.pop_ogate_and_packets();
    bess::OGate *ogate = reinterpret_cast<bess::OGate *>(task.gate);
    bess::PacketBatch *next_packets = &(task.batch);

    for (auto &hook : ogate->hooks()) {
      hook->ProcessBatch(next_packets);
    }

    for (auto &hook : ogate->igate()->hooks()) {
      hook->ProcessBatch(next_packets);
    }

    ctx.set_current_igate(ogate->igate_idx());
    ((Module *)ogate->arg())->ProcessBatch(next_packets);
  }

  return ret;
}
