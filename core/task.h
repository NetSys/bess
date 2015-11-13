#ifndef _TASK_H_
#define _TASK_H_

#include <stdint.h>

#include "utils/cdlist.h"

#include "tc.h"

extern struct cdlist_head all_tasks;

typedef uint16_t task_id_t;

#define INVALID_TASK_ID		((task_id_t)-1)

struct task_result {
	uint64_t packets;
	uint64_t bits;
};

struct module;

typedef struct task_result (*task_func_t)(struct module *, void *);

struct task {
	struct tc *c;

	struct module *m;
	task_func_t f;
	void *arg;

	struct cdlist_item tc;
	struct cdlist_item all_tasks;
};

struct task *task_create(struct module *m, void *arg);
void task_destroy(struct task *t);

static inline int task_is_attached(struct task *t)
{
	return (t->c != NULL);
}

void task_attach(struct task *t, struct tc *c);
void task_detach(struct task *t);

static inline struct task_result task_scheduled(struct task *t)
{
	return t->f(t->m, t->arg);
}

void assign_default_tc(int wid, struct task *t);
void process_orphan_tasks();

#endif
