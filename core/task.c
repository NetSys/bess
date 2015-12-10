#include <assert.h>

#include <rte_malloc.h>

#include "task.h"
#include "module.h"

struct cdlist_head all_tasks = CDLIST_HEAD_INIT(all_tasks);

struct task *task_create(struct module *m, void *arg)
{
	struct task *t;

	t = rte_zmalloc("task", sizeof(*t), 0);
	if (!t)
		oom_crash();
	
	cdlist_item_init(&t->tc);
	cdlist_add_tail(&all_tasks, &t->all_tasks);
	
	t->m = m;
	t->f = m->mclass->run_task;
	t->arg = arg;

	return t;
}

void task_destroy(struct task *t)
{
	if (task_is_attached(t))
		task_detach(t);

	cdlist_del(&t->all_tasks);
	rte_free(t);
}

void task_attach(struct task *t, struct tc *c)
{
	if (t->c) {
		if (t->c == c)		/* already attached to TC c? */
			return;

		task_detach(t);
	}

	t->c = c;
	cdlist_add_tail(&c->tasks, &t->tc);
	tc_inc_refcnt(c);
	c->num_tasks++;
}

void task_detach(struct task *t)
{
	struct tc *c = t->c;

	if (!task_is_attached(t))
		return;

	t->c = NULL;
	cdlist_del(&t->tc);
	tc_dec_refcnt(c);
	c->num_tasks--;

	/* c is up for autofree, and the task t was the last one standing? */
	if (cdlist_is_empty(&c->tasks) && c->auto_free) {
		tc_leave(c);		/* stop scheduling this TC */
		tc_dec_refcnt(c);	/* release my reference */
	}
}

void assign_default_tc(int wid, struct task *t)
{
	static int next_default_tc_id;

	struct tc *c_def;

	struct tc_params params = {
		.parent = NULL,
		.auto_free = 1,	/* when no task is left, this TC is freed */
		.priority = DEFAULT_PRIORITY,
		.share = 1,
		.share_resource = RESOURCE_CNT,
	};

	do {
		sprintf(params.name, "tc_orphan%d", next_default_tc_id++);
		c_def = tc_init(workers[wid]->s, &params);
	} while (ptr_to_err(c_def) == -EEXIST);
	
	if (is_err(c_def)) {
		fprintf(stderr, "tc_init() failed\n");
		return;
	}

	task_attach(t, c_def);
	tc_join(c_def);
}

static int get_next_wid(int *wid)
{
	static int rr_next = 0;

	if (num_workers == 0)
		return -1;

	while (!is_worker_active(rr_next))
		rr_next = (rr_next + 1) % MAX_WORKERS;

	*wid = rr_next;
	rr_next = (rr_next + 1) % MAX_WORKERS;

	return 0;
}

/* Spread all orphan tasks across workers with round robin */
void process_orphan_tasks()
{
	struct task *t;

	cdlist_for_each_entry(t, &all_tasks, all_tasks) {
		int wid;

		if (task_is_attached(t))
			continue;

		if (get_next_wid(&wid) < 0) {
			wid = 0;
			/* There is no active worker. Create one. */
			launch_worker(wid, 0);
		}

		assign_default_tc(wid, t);
	}
}
