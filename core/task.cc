#include <assert.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "module.h"
#include "tc.h"

// Capture the default core command line flag.
DECLARE_int32(c);

struct cdlist_head all_tasks = CDLIST_HEAD_INIT(all_tasks);

struct task *task_create(Module *m, void *arg) {
  struct task *t;

  t = (struct task *)mem_alloc(sizeof(*t));
  if (!t) return NULL;

  cdlist_item_init(&t->tc);
  cdlist_add_tail(&all_tasks, &t->all_tasks);

  t->m = m;
  t->arg = arg;

  return t;
}

void task_destroy(struct task *t) {
  if (task_is_attached(t)) task_detach(t);

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

  if (!task_is_attached(t)) return;

  t->c = NULL;
  cdlist_del(&t->tc);
  tc_dec_refcnt(c);
  c->num_tasks--;

  /* c is up for autofree, and the task t was the last one standing? */
  if (cdlist_is_empty(&c->tasks) && c->settings.auto_free) {
    tc_leave(c);      /* stop scheduling this TC */
    tc_dec_refcnt(c); /* release my reference */
  }
}

#include <iostream>
void assign_default_tc(int wid, struct task *t) {
  static int next_default_tc_id;

  struct tc *c_def;

  struct tc_params params = {};

  params.parent = NULL;
  params.auto_free = 1; /* when no task is left, this TC is freed */
  params.priority = DEFAULT_PRIORITY;
  params.share = 1;
  params.share_resource = RESOURCE_CNT;

  printf("%p\n", t->m);

  if (t->m->NumTasks() == 1)
    sprintf(params.name, "_tc_%s", t->m->name().c_str());
  else
    sprintf(params.name, "_tc_%s_%d", t->m->name().c_str(), task_to_tid(t));

  c_def = tc_init(workers[wid]->s(), &params);

  /* maybe the default name is too long, or already occupied */
  if (is_err_or_null(c_def)) {
    do {
      sprintf(params.name, "_tc_noname%d", next_default_tc_id++);
      c_def = tc_init(workers[wid]->s(), &params);
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

  if (num_workers == 0) return -1;

  while (!is_worker_active(rr_next)) rr_next = (rr_next + 1) % MAX_WORKERS;

  *wid = rr_next;
  rr_next = (rr_next + 1) % MAX_WORKERS;

  return 0;
}

/* Spread all orphan tasks across workers with round robin */
void process_orphan_tasks() {
  struct task *t;

  cdlist_for_each_entry(t, &all_tasks, all_tasks) {
    int wid;

    if (task_is_attached(t)) continue;

    if (get_next_wid(&wid) < 0) {
      wid = 0;
      /* There is no active worker. Create one. */
      launch_worker(wid, FLAGS_c);
    }

    assign_default_tc(wid, t);
  }
}
