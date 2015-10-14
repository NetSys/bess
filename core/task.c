#include <assert.h>

#include <rte_malloc.h>

#include "task.h"
#include "module.h"

struct cdlist_head all_tasks = CDLIST_HEAD_INIT(all_tasks);

struct task *task_create(struct module *m, void *arg)
{
	struct task *t;

	if (!m->mclass->run_task)
		return NULL;

	t = rte_zmalloc("task", sizeof(*t), 0);
	if (!t)
		oom_crash();
	
	cdlist_item_init(&t->tc);
	cdlist_add_tail(&m->tasks, &t->module);
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
	cdlist_del(&t->module);
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
}

void task_detach(struct task *t)
{
	struct tc *c = t->c;

	if (!task_is_attached(t))
		return;

	t->c = NULL;
	cdlist_del(&t->tc);
	tc_dec_refcnt(c);

	/* c is up for autofree, and the task t was the last one standing? */
	if (cdlist_is_empty(&c->tasks) && c->auto_free) {
		tc_leave(c);		/* stop scheduling this TC */
		tc_dec_refcnt(c);	/* release my reference */
	}
}
