#include "../module.h"

static struct task_result
noop_run_task(struct module *m, void *arg)
{
	struct task_result ret;

	ret = (struct task_result) {
		.packets = 0,
		.bits = 0,
	};

	return ret;
}

static const struct mclass noop = {
	.name 		= "NoOP",
	.help		= "creates a task that does nothing",
	.num_igates	= 0,
	.num_ogates	= 0,
	.run_task 	= noop_run_task,
};

ADD_MCLASS(noop)
