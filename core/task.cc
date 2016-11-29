#include "task.h"

#include <cassert>

#include <glog/logging.h>

#include "mem_alloc.h"
#include "module.h"
#include "opts.h"
#include "tc.h"
#include "worker.h"

struct cdlist_head all_tasks = CDLIST_HEAD_INIT(all_tasks);

struct task_result task_scheduled(struct task *t) {
  struct task_result ret = t->m->RunTask(t->arg);

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

struct task *task_create(Module *m, void *arg) {
  struct task *t;

  t = (struct task *)mem_alloc(sizeof(*t));
  if (!t) {
    return nullptr;
  }

  cdlist_item_init(&t->tc);
  cdlist_add_tail(&all_tasks, &t->all_tasks);

  t->m = m;
  t->arg = arg;

  return t;
}

void task_destroy(struct task *t) {
  if (task_is_attached(t))
    task_detach(t);

  cdlist_del(&t->all_tasks);
  mem_free(t);
}

void task_attach(struct task *t, struct tc *c) {
  if (t->c) {
    if (t->c == c) /* already attached to TC c? */
      return;

    task_detach(t);
  }

  t->c = c;
  cdlist_add_tail(&c->tasks, &t->tc);
  tc_inc_refcnt(c);
  c->num_tasks++;
}

void task_detach(struct task *t) {
  struct tc *c = t->c;

  if (!task_is_attached(t))
    return;

  t->c = nullptr;
  cdlist_del(&t->tc);
  tc_dec_refcnt(c);
  c->num_tasks--;

  /* c is up for autofree, and the task t was the last one standing? */
  if (cdlist_is_empty(&c->tasks) && c->settings.auto_free) {
    tc_leave(c);      /* stop scheduling this TC */
    tc_dec_refcnt(c); /* release my reference */
  }
}

void assign_default_tc(int wid, struct task *t) {
  static int next_default_tc_id;

  struct tc *c_def;

  struct tc_params params;

  if (t->m->NumTasks() == 1) {
    params.name = "_tc_" + t->m->name();
  } else {
    params.name = "_tc_" + t->m->name() + std::to_string(task_to_tid(t));
  }

  params.auto_free = 1; /* when no task is left, this TC is freed */
  params.priority = DEFAULT_PRIORITY;
  params.share = 1;
  params.share_resource = RESOURCE_CNT;

  c_def = tc_init(workers[wid]->s(), &params, nullptr);

  /* maybe the default name is too long, or already occupied */
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

/* Spread all orphan tasks across workers with round robin */
void process_orphan_tasks() {
  struct task *t;

  cdlist_for_each_entry(t, &all_tasks, all_tasks) {
    int wid;

    if (task_is_attached(t))
      continue;

    if (get_next_wid(&wid) < 0) {
      wid = 0;
      /* There is no active worker. Create one. */
      launch_worker(wid, FLAGS_c);
    }

    assign_default_tc(wid, t);
  }
}
