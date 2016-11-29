#include "task.h"

#include <cassert>

#include <glog/logging.h>

#include "mem_alloc.h"
#include "module.h"
#include "opts.h"
#include "traffic_class.h"
#include "worker.h"

Task::Task(Module *m, void *arg, bess::LeafTrafficClass *c) : m_(m), arg_(arg), c_(c) {
  // TODO(barath): Require that the class c be not null.
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

/*
void assign_default_tc(int wid, struct task *t) {
  static int next_default_tc_id;

  struct tc *c_def;

  struct tc_params params;

  if (t->m->NumTasks() == 1) {
    params.name = "_tc_" + t->m->name();
  } else {
    params.name = "_tc_" + t->m->name() + std::to_string(task_to_tid(t));
  }

  params.auto_free = 1; // when no task is left, this TC is freed
  params.priority = DEFAULT_PRIORITY;
  params.share = 1;
  params.share_resource = RESOURCE_CNT;

  c_def = tc_init(workers[wid]->s(), &params, nullptr);

  // maybe the default name is too long, or already occupied
  if (is_err_or_null(c_def)) {
    do {
      params.name = "_tc_noname" + std::to_string(next_default_tc_id++);
      c_def = tc_init(workers[wid]->s(), &params, nullptr);
    } while (ptr_to_err(c_def) == -EEXIST);
  }

  if (is_err(c_def)) {
    LOG(ERROR) << "tc_init() failed";
    return;
  }

  task_attach(t, c_def);
  tc_join(c_def);
}

static int get_next_wid(int *wid) {
  static int rr_next = 0;

  if (num_workers == 0)
    return -1;

  while (!is_worker_active(rr_next)) {
    rr_next = (rr_next + 1) % MAX_WORKERS;
  }

  *wid = rr_next;
  rr_next = (rr_next + 1) % MAX_WORKERS;

  return 0;
}

// Spread all orphan tasks across workers with round robin
void process_orphan_tasks() {
  struct task *t;

  cdlist_for_each_entry(t, &all_tasks, all_tasks) {
    int wid;

    if (task_is_attached(t))
      continue;

    if (get_next_wid(&wid) < 0) {
      wid = 0;
      // There is no active worker. Create one.
      launch_worker(wid, FLAGS_c);
    }

    assign_default_tc(wid, t);
  }
}
*/
