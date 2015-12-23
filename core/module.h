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

#include "debug.h"
#include "mclass.h"
#include "snbuf.h"
#include "worker.h"
#include "snobj.h"

#define MAX_TASKS_PER_MODULE	32

ct_assert(MAX_TASKS_PER_MODULE < INVALID_TASK_ID);

#define MODULE_NAME_LEN		128

typedef uint16_t gate_t;

#define MAX_OUTPUT_GATES	8192
#define INVALID_GATE		UINT16_MAX

ct_assert(MAX_OUTPUT_GATES < INVALID_GATE);

#define TRACK_GATES		1
#define TCPDUMP_GATES		1

struct output_gate {
	struct module *m;
	proc_func_t f;		/* m->mclass->process_batch() or deadend() */
#if TRACK_GATES
	uint64_t cnt;
	uint64_t pkts;
#endif
#if TCPDUMP_GATES
	uint32_t tcpdump;
	int fifo_fd;
#endif
};

/* This struct is shared across workers */
struct module {
	/* less frequently accessed fields should be here */
	char *name;

	const struct mclass *mclass;

	struct task *tasks[MAX_TASKS_PER_MODULE];

	/* frequently access fields should be below */
	gate_t allocated_gates;
	struct output_gate *gates;

	/* Some private data for this module instance begins at this marker. 
	 * (this is poor person's class inheritance in C language)
	 * The 'struct module' object will be allocated with enough tail room
	 * to accommodate this private data. It is initialized with zeroes.
	 * We don't do dynamic allocation for private data, 
	 * to save a few cycles without indirect memory access.
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
void unregister_task(struct module *m, task_id_t tid);

size_t list_modules(const struct module **p_arr, size_t arr_size, size_t offset);

struct module *find_module(const char *name);

struct module *create_module(const char *name, 
		const struct mclass *class, 
		struct snobj *arg,
		struct snobj **perr);

void destroy_module(struct module *m);

int connect_modules(struct module *m1, gate_t gate, struct module *m2);
int disconnect_modules(struct module *m, gate_t gate);
		
void deadend(struct module *m, struct pkt_batch *batch);

/* run all per-thread initializers */
void init_module_worker(void);

#if SN_TRACE_MODULES
void _trace_before_call(struct module *mod, struct module *next,
			struct pkt_batch *batch);

void _trace_after_call(void);
#endif

#if TCPDUMP_GATES
int enable_tcpdump(const char* fifo, struct module *m, gate_t gate);

int disable_tcpdump(struct module *m, gate_t gate);

void dump_pcap_pkts(struct output_gate *gate, struct pkt_batch *batch);

#else
inline int enable_tcpdump(const char *, struct module *, gate_t) {
	/* Cannot enable tcpdump */
	return -EINVAL;
}

inline int disable_tcpdump(struct module *, int) {
	/* Cannot disable tcpdump */
	return -EINVAL;
}
#endif


/* Pass packets to the next module.
 * Packet deallocation is callee's responsibility. */
static inline void run_choose_module(struct module *m, gate_t ogate,
				     struct pkt_batch *batch)
{
	struct output_gate *gate;

	if (unlikely(ogate >= m->allocated_gates)) {
		deadend(NULL, batch);
		return;
	}

	gate = &m->gates[ogate];

#if SN_TRACE_MODULES
	_trace_before_call(m, next, batch);
#endif

#if TRACK_GATES
	gate->cnt += 1;
	gate->pkts += batch->cnt;
#endif

#if TCPDUMP_GATES
	if (unlikely(gate->tcpdump))
		dump_pcap_pkts(gate, batch);
#endif

	gate->f(gate->m, batch);

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
static void run_split(struct module *m, const gate_t *ogates,
		struct pkt_batch *mixed_batch)
{
	int cnt = mixed_batch->cnt;
	int num_pending = 0;

	snb_array_t p_pkt = &mixed_batch->pkts[0];

	gate_t pending[MAX_PKT_BURST];
	struct pkt_batch batches[MAX_PKT_BURST];

	struct pkt_batch *splits = ctx.splits;

	/* phase 1: collect unique ogates into pending[] */
	for (int i = 0; i < cnt; i++) {
		struct pkt_batch *batch;
		gate_t ogate;
		
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
