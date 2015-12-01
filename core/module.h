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

static inline int dump_pcap_pkts(int fd, struct pkt_batch *batch)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	int ret = 0;
	int packets = 0;
	int error_out = 0;
	for (int i = 0; i < batch->cnt && (!error_out); i++) {
		struct snbuf* pkt = batch->pkts[i];
		int len = pkt->mbuf.data_len;
		struct pcap_rec_hdr *pkthdr = 
			(struct pcap_rec_hdr*) snb_prepend(pkt, 
				sizeof(struct pcap_rec_hdr));
		pkthdr->ts_sec = tv.tv_sec;
		pkthdr->ts_usec = tv.tv_usec;
		pkthdr->orig_len = pkthdr->incl_len = len;
		assert(len < PCAP_SNAPLEN);
		ret = write(fd, snb_head_data(pkt), pkt->mbuf.data_len);
		assert(pkt->mbuf.data_len < PIPE_BUF);
		if (ret < 0) {
			if (errno == EPIPE) {
				printf("Stopping dump\n");
				close(fd);
				packets = -1;
			}
			error_out = 1;
		} else {
			assert(ret == pkt->mbuf.data_len);
			packets++;
		}
		snb_adj(pkt, sizeof(struct pcap_rec_hdr));
	}
	return packets;
}
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
	if (gate->tcpdump) {
		int ret = dump_pcap_pkts(gate->fifo_fd, batch);
		/* Not only did the previous operation fail, 
		 * but fd was closed */
		if (ret < 0) {
			gate->tcpdump = 0;
			gate->fifo_fd = 0;
		}
	}
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
		const struct pkt_batch *mixed_batch)
{
	gate_t pending[MAX_PKT_BURST];
	int num_pending = 0;

	/* collect */
	for (int i = 0; i < mixed_batch->cnt; i++) {
		struct pkt_batch *batch;
		gate_t ogate;
		
		ogate = ogates[i];
		batch = &ctx.splits[ogate];

		/* branchless version didn't help */
		if (batch->cnt == 0)
			pending[num_pending++] = ogate;

		batch_add(batch, mixed_batch->pkts[i]);
	}

	/* fire */
	for (int i = 0; i < num_pending; i++) {
		struct pkt_batch *batch;
		gate_t ogate;

		ogate = pending[i];
		batch = &ctx.splits[ogate];

		run_choose_module(m, ogate, batch);
		batch_clear(batch);
	}
}

#if 0
#define SN_TRACE_MODULES	0
#define SN_CPU_USAGE		1

struct module {
	char name[MODULE_NAME_LEN];

	const struct mclass *mclass;

	int num_next_modules;
	struct module *next_modules[MAX_NEXT_MODULES];

	struct rte_timer *timers[MAX_WORKERS];

	void *priv_shared; 	/* Note: this is shared across SoftNIC workers */
	void *priv_worker[MAX_WORKERS];
};

#define set_priv_worker(mod, data) \
	do { mod->priv_worker[ctx.wid] = data; } while (0)

#define get_priv_worker(mod, type) \
	((type)mod->priv_worker[ctx.wid])
/* for single-output modules */
void set_next_module(struct module *prev, struct module *next);

/* for multi-output modules */
void add_next_module(struct module *prev, struct module *next, void *arg);

static inline int do_poll(struct module *mod)
{
	int ret;

#if SN_TRACE_MODULES
	_trace_start(mod, "POLL");
#endif

	ret = mod->ops->scheduled(mod);

#if SN_TRACE_MODULES
	_trace_end(ret != 0);
#endif

	return ret;
}

void dpdk_timer_cb(struct rte_timer *timer, void *arg);

static inline void _reset_timer(struct module *mod, uint64_t us,
				enum rte_timer_type type)
{
	rte_timer_reset_sync(mod->timers[current_wid],
			us * (rte_get_timer_hz() / 1000000), type,
			wid_to_lcore_map[current_wid], dpdk_timer_cb, mod);
}

/* It triggers a call to ops->timer() on the current core,
 * us microseconds later (one shot). */
static inline void reset_timer_single(struct module *mod, uint64_t us)
{
	_reset_timer(mod, us, SINGLE);
}

/* It triggers a call to ops->timer() on the current core,
 * for every us microseconds. */
static inline void reset_timer_periodic(struct module *mod, uint64_t us)
{
	_reset_timer(mod, us, PERIODICAL);
}
#endif

#endif
