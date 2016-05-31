#ifndef _MCLASS_H_
#define _MCLASS_H_

#include "task.h"
#include "metadata.h"

#define ADD_MCLASS(mc) \
	static const struct mclass mc; \
	__attribute__((constructor(103))) void __mclass_register_##mc() \
	{ \
		int ret; \
		ret = add_mclass(&mc); \
		assert(ret == 0); \
	}

typedef uint16_t gate_idx_t;

#define INVALID_GATE		UINT16_MAX

/* A module may have up to MAX_GATES input/output gates (separately). */
#define MAX_GATES		8192 
#define DROP_GATE		MAX_GATES
ct_assert(MAX_GATES < INVALID_GATE);
ct_assert(DROP_GATE <= MAX_GATES);

#define MAX_COMMANDS		32

struct module;
struct pkt_batch;
struct snobj;

typedef void (*proc_func_t) (struct module *, struct pkt_batch *);

typedef struct snobj *
(*cmd_func_t) (struct module *, const char *, struct snobj *);

struct mclass
{
	/* Required: should be like "CamelCase" */
	const char *name;

	/* Optional: one-line description of the module class */
	const char *help;

	/* Optional: should be like "lower_case". 
	 * - "%d" is automatically appended.
	 * - Anonymous modules will have a default name source0, source1, ... 
	 * - If this field is not provided, the mclass name will be used 
	 *   after auto transformation (CamelCase -> camel_case) */
	const char *def_module_name;

	/* Required: the maximum number of input/output gates (can be 0) */
	gate_idx_t num_igates;
	gate_idx_t num_ogates;

	/* Optional: the size of per-module private data. 0 by default.
	 *   The memory region will be zero initialized. */
	uint32_t priv_size;

	/* Optional: list of metadata attributes */
	struct mt_attr attrs[MAX_ATTRS_PER_MODULE];

	/* Optional: perform any necessary initialization.
	 * Should return NULL if successful, or snobj_err_*() 
	 * If this mclass implements run_task, this init function
	 * 	should register its tasks, so that the scheduler can trigger
	 * 	the tasks.
	 * arg can be NULL, if not given. */
	struct snobj *(*init)(struct module *m, struct snobj *arg);

	/* Optional: cleanup internal state */
	void (*deinit)(struct module *m);

#if 0
	/* FIXME */
	/* Optional: Invoked on every worker */
	void (*init_worker)(struct module *m);
#endif

	/* Optional: return human-readable very short description of module
	 *           e.g., "port/PMD". Type must be a string */
	struct snobj *(*get_desc)(const struct module *m);

	/* Optional: return any object type. Module-specific semantics. */
	struct snobj *(*get_dump)(const struct module *m);

	/* The (abstract) call stack would be:
	 *   sched -> task -> module1.run_task -> 
	 *   		module2.process_batch -> module3.process_batch -> ... */

	/* Optional: Triggered by its previous module */
	proc_func_t process_batch;
	
	/* The entry point of the packet packet processing pipeline */
	task_func_t run_task;

	const struct {
		const char *cmd;
		cmd_func_t func;

		/* if non-zero, workers don't need to be paused in order to
		 * run this command */
		int mt_safe;
	} commands[MAX_COMMANDS];
};

size_t list_mclasses(const struct mclass **p_arr, size_t arr_size, 
		size_t offset);

const struct mclass *find_mclass(const char *name);

/* returns -1 if fails */
int add_mclass(const struct mclass *mclass);

static inline int is_valid_gate(gate_idx_t gate)
{
	return (gate < MAX_GATES || gate == DROP_GATE);
}

#endif
