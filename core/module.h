#ifndef _MODULE_H_
#define _MODULE_H_

#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <rte_timer.h>
#include <rte_cycles.h>

#include "utils/cdlist.h"
#include "utils/pcap.h"

#include "log.h"
#include "debug.h"
#include "mclass.h"
#include "snbuf.h"
#include "worker.h"
#include "snobj.h"

#define MAX_TASKS_PER_MODULE	32

ct_assert(MAX_TASKS_PER_MODULE < INVALID_TASK_ID);

#define MODULE_NAME_LEN		128

#define TRACK_GATES		1
#define TCPDUMP_GATES		1

struct gate {
	/* immutable values */
	struct module *m;	/* the module this gate belongs to */
	gate_idx_t gate_idx;	/* input/output gate index of itself */

	/* mutable values below */
	proc_func_t f;		/* m_next->mclass->process_batch or deadend */
	void *arg;

	union {
		struct {
			struct cdlist_item igate_upstream; 
			struct gate *igate;
			gate_idx_t igate_idx;
		} out;

		struct {
			struct cdlist_head ogates_upstream;
		} in;
	};

	/* TODO: generalize with gate hooks */
#if TRACK_GATES
	uint64_t cnt;
	uint64_t pkts;
#endif
#if TCPDUMP_GATES
	uint32_t tcpdump;
	int fifo_fd;
#endif
};

struct gates {
	/* Resizable array of 'struct gate *'. 
	 * Unconnected elements are filled with NULL */
	struct gate **arr;	

	/* The current size of the array.
	 * Always <= m->mclass->num_[i|o]gates */
	gate_idx_t curr_size;
};

static inline int is_active_gate(struct gates *gates, gate_idx_t idx)
{
	return idx < gates->curr_size && gates->arr[idx] != NULL;
}

/* This struct is shared across workers */
struct module {
	/* less frequently accessed fields should be here */
	char *name;
	const struct mclass *mclass;
	struct task *tasks[MAX_TASKS_PER_MODULE];

	/* frequently access fields should be below */
	struct gates igates;
	struct gates ogates;

	/* Some private data for this module instance begins at this marker. 
	 * (this is poor person's class inheritance in C language)
	 * The 'struct module' object will be allocated with enough tail room
	 * to accommodate this private data. It is initialized with zeroes.
	 * We don't do dynamic allocation for private data, 
	 * to save a few cycles by avoiding indirect memory access.
	 *
	 * Note: this is shared across all workers. Ensuring thread safety 
	 * and/or managing per-worker data is each module's responsibility. */
	void *priv[0]; 	
};

static inline void *get_priv(struct module *m) 
{
	return (void *)(m + 1);
}

static inline const void *get_priv_const(const struct module *m) 
{
	return (const void *)(m + 1);
}

task_id_t register_task(struct module *m, void *arg);
task_id_t task_to_tid(struct task *t);
int num_module_tasks(struct module *m);

size_t list_modules(const struct module **p_arr, size_t arr_size, size_t offset);

struct module *find_module(const char *name);

struct module *create_module(const char *name, 
		const struct mclass *class, 
		struct snobj *arg,
		struct snobj **perr);

void destroy_module(struct module *m);

int connect_modules(struct module *m_prev, gate_idx_t ogate_idx, 
		    struct module *m_next, gate_idx_t igate_idx);
int disconnect_modules(struct module *m_prev, gate_idx_t ogate_idx);
		
void deadend(struct module *m, struct pkt_batch *batch);

/* run all per-thread initializers */
void init_module_worker(void);

#if SN_TRACE_MODULES
void _trace_before_call(struct module *mod, struct module *next,
			struct pkt_batch *batch);

void _trace_after_call(void);
#endif

#if TCPDUMP_GATES
int enable_tcpdump(const char* fifo, struct module *m, gate_idx_t gate);

int disable_tcpdump(struct module *m, gate_idx_t gate);

void dump_pcap_pkts(struct gate *gate, struct pkt_batch *batch);

#else
inline int enable_tcpdump(const char *, struct module *, gate_idx_t) {
	/* Cannot enable tcpdump */
	return -EINVAL;
}

inline int disable_tcpdump(struct module *, int) {
	/* Cannot disable tcpdump */
	return -EINVAL;
}
#endif


static inline gate_idx_t get_igate()
{
	return ctx.igate_stack[ctx.stack_depth - 1];
}

/* Pass packets to the next module.
 * Packet deallocation is callee's responsibility. */
static inline void run_choose_module(struct module *m, gate_idx_t ogate_idx,
				     struct pkt_batch *batch)
{
	struct gate *ogate;

	if (unlikely(ogate_idx >= m->ogates.curr_size)) {
		deadend(NULL, batch);
		return;
	}

	ogate = m->ogates.arr[ogate_idx];

	if (unlikely(!ogate)) {
		deadend(NULL, batch);
		return;
	}

#if SN_TRACE_MODULES
	_trace_before_call(m, next, batch);
#endif

#if TRACK_GATES
	ogate->cnt += 1;
	ogate->pkts += batch->cnt;
#endif

#if TCPDUMP_GATES
	if (unlikely(ogate->tcpdump))
		dump_pcap_pkts(ogate, batch);
#endif

	ctx.igate_stack[ctx.stack_depth] = ogate->out.igate_idx;
	ctx.stack_depth++;

	ogate->f(ogate->arg, batch);

	ctx.stack_depth--;

#if SN_TRACE_MODULES
	_trace_after_call();
#endif
}

/* Wrapper for single-output modules */
static inline void run_next_module(struct module *m, struct pkt_batch *batch)
{
	run_choose_module(m, 0, batch);
}

/*
 * Split a batch into several, one for each ogate
 * NOTE:
 *   1. Order is preserved for packets with the same gate.
 *   2. No ordering guarantee for packets with different gates.
 */
static void run_split(struct module *m, const gate_idx_t *ogates,
		struct pkt_batch *mixed_batch)
{
	int cnt = mixed_batch->cnt;
	int num_pending = 0;

	snb_array_t p_pkt = &mixed_batch->pkts[0];

	gate_idx_t pending[MAX_PKT_BURST];
	struct pkt_batch batches[MAX_PKT_BURST];

	struct pkt_batch *splits = ctx.splits;

	/* phase 1: collect unique ogates into pending[] */
	for (int i = 0; i < cnt; i++) {
		struct pkt_batch *batch;
		gate_idx_t ogate;
		
		ogate = ogates[i];
		batch = &splits[ogate];

		batch_add(batch, *(p_pkt++));

		pending[num_pending] = ogate;
		num_pending += (batch->cnt == 1);
	}

	/* phase 2: move batches to local stack, since it may be reentrant */
	for (int i = 0; i < num_pending; i++) {
		struct pkt_batch *batch;

		batch = &splits[pending[i]];
		batch_copy(&batches[i], batch);
		batch_clear(batch);
	}

	/* phase 3: fire */
	for (int i = 0; i < num_pending; i++)
		run_choose_module(m, pending[i], &batches[i]);
}

#endif
