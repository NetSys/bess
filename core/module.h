#ifndef _CC_MODULE_H_
#define _CC_MODULE_H_

#include <cassert>
#include <string>
#include <vector>

#include "common.h"

typedef uint16_t task_id_t;
typedef uint16_t gate_idx_t;

#define INVALID_GATE		UINT16_MAX

/* A module may have up to MAX_GATES input/output gates (separately). */
#define MAX_GATES		8192
#define DROP_GATE		MAX_GATES
ct_assert(MAX_GATES < INVALID_GATE);
ct_assert(DROP_GATE <= MAX_GATES);

#include "snobj.h"
#include "metadata.h"
#include "worker.h"
#include "snbuf.h"
#include "utils/cdlist.h"

#define MODULE_NAME_LEN		128

#define TRACK_GATES		1
#define TCPDUMP_GATES		1

#define MAX_TASKS_PER_MODULE	32
#define INVALID_TASK_ID		((task_id_t)-1)

ct_assert(MAX_TASKS_PER_MODULE < INVALID_TASK_ID);

struct task_result {
	uint64_t packets;
	uint64_t bits;
};

class Module;

struct gate {
	/* immutable values */
	Module *m;	/* the module this gate belongs to */
	gate_idx_t gate_idx;	/* input/output gate index of itself */

	/* mutable values below */
	void *arg;

	union {
		struct {
			struct cdlist_item igate_upstream;
			struct gate *igate;
			gate_idx_t igate_idx;	/* cache for igate->gate_idx */
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

#define CALL_MEMBER_FN(obj, ptr_to_member_func) ((obj).*(ptr_to_member_func))

typedef struct snobj *(Module::*CmdFunc)(struct snobj *);

struct Command {
	std::string cmd;
	CmdFunc func;

	// if non-zero, workers don't need to be paused in order to
	// run this command
	int mt_safe;
};

class ModuleClass {
public:
	ModuleClass(const std::string &class_name,
		    const std::string &name_template,
		    const std::string &help) :
			name_(class_name),
			name_template_(name_template),
			help_(help) {
		int ret = ns_insert(NS_TYPE_MCLASS, class_name.c_str(),
				static_cast<void *>(this));
		if (ret < 0) {
			log_err("ns_insert() failure for module class '%s'\n",
					class_name.c_str());
		}
	}

	virtual Module *CreateModule(const std::string &name) const = 0;

	std::string Name() const { return name_; }
	std::string NameTemplate() const {return name_template_; }
	std::string Help() const { return help_; }

	virtual gate_idx_t NumIGates() const = 0;
	virtual gate_idx_t NumOGates() const = 0;

	std::vector<struct Command> cmds;

private:
	std::string name_;
	std::string name_template_;
	std::string help_;
};

template <typename T>
class ModuleClassRegister : public ModuleClass {
public:
	ModuleClassRegister(const std::string &class_name,
		    const std::string &name_template,
		    const std::string &help) :
		ModuleClass(class_name, name_template, help) {};

	virtual Module *CreateModule(const std::string &name) const {
		T *m = new T;
		m->name_ = name;
		m->mclass_ = this;
		return m;
	};

	virtual gate_idx_t NumIGates() const { return T::kNumIGates; }
	virtual gate_idx_t NumOGates() const { return T::kNumOGates; }
};

class Module {
// overide this section to create a new module -----------------------------
public:
	Module() = default;
	virtual ~Module() = 0;

	virtual struct snobj *Init(struct snobj *arg) { return nullptr; };
	virtual void Deinit() {};

	virtual struct task_result RunTask(void *arg) { assert(0); };
	virtual void ProcessBatch(struct pkt_batch *batch) { assert(0); };

	virtual struct snobj *GetDesc() const { return snobj_str(""); };
	virtual struct snobj *GetDump() const { return snobj_nil(); };

// -------------------------------------------------------------------------

public:
	const ModuleClass *Class() const { return mclass_; };
//	std::string ClassName() const { return mclass_->Name(); };
	std::string Name() const { return name_; };

	struct snobj *RunCommand(const std::string &user_cmd, struct snobj *arg);

private:
	// non-copyable and non-movable by default
	Module(Module&) = delete;
	Module& operator=(Module&) = delete;
	Module(Module&&) = delete;
	Module& operator=(Module&&) = delete;

	std::string name_;
	const ModuleClass *mclass_;

	template<typename T> friend class ModuleClassRegister;

// FIXME: porting in progress ----------------------------
public:
	struct task *tasks[MAX_TASKS_PER_MODULE] = {};

	int num_attrs = 0;
	struct mt_attr attrs[MAX_ATTRS_PER_MODULE] = {};
	scope_id_t scope_components[MT_TOTAL_SIZE] = {};

	int curr_scope = 0;

	mt_offset_t attr_offsets[MAX_ATTRS_PER_MODULE] = {};
	struct gates igates = {};
	struct gates ogates = {};
};

task_id_t register_task(Module *m, void *arg);

struct task {
	struct tc *c;

	Module *m;
	void *arg;

	struct cdlist_item tc;
	struct cdlist_item all_tasks;
};

struct task *task_create(Module *m, void *arg);

void task_destroy(struct task *t);

static inline int task_is_attached(struct task *t)
{
	return (t->c != NULL);
}

void task_attach(struct task *t, struct tc *c);
void task_detach(struct task *t);

static inline struct task_result task_scheduled(struct task *t)
{
	return t->m->RunTask(t->arg);
}

void assign_default_tc(int wid, struct task *t);
void process_orphan_tasks();

task_id_t task_to_tid(struct task *t);
int num_module_tasks(Module *m);

size_t list_modules(const Module **p_arr, size_t arr_size, size_t offset);

Module *find_module(const char *name);

Module *create_module(const char *name,
		const ModuleClass *mclass,
		struct snobj *arg,
		struct snobj **perr);

void destroy_module(Module *m);

int connect_modules(Module *m_prev, gate_idx_t ogate_idx,
		    Module *m_next, gate_idx_t igate_idx);
int disconnect_modules(Module *m_prev, gate_idx_t ogate_idx);

void deadend(Module *m, struct pkt_batch *batch);

/* run all per-thread initializers */
void init_module_worker(void);

#if SN_TRACE_MODULES
void _trace_before_call(Module *mod, Module *next,
			struct pkt_batch *batch);

void _trace_after_call(void);
#endif

#if TCPDUMP_GATES
int enable_tcpdump(const char* fifo, Module *m, gate_idx_t gate);

int disable_tcpdump(Module *m, gate_idx_t gate);

void dump_pcap_pkts(struct gate *gate, struct pkt_batch *batch);

#else
inline int enable_tcpdump(const char *, Module *, gate_idx_t) {
	/* Cannot enable tcpdump */
	return -EINVAL;
}

inline int disable_tcpdump(Module *, int) {
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
static inline void run_choose_module(Module *m, gate_idx_t ogate_idx,
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

	// XXX
	((Module *)ogate->arg)->ProcessBatch(batch);

	ctx.stack_depth--;

#if SN_TRACE_MODULES
	_trace_after_call();
#endif
}

/* Wrapper for single-output modules */
static inline void run_next_module(Module *m, struct pkt_batch *batch)
{
	run_choose_module(m, 0, batch);
}

/*
 * Split a batch into several, one for each ogate
 * NOTE:
 *   1. Order is preserved for packets with the same gate.
 *   2. No ordering guarantee for packets with different gates.
 */
static void run_split(Module *m, const gate_idx_t *ogates,
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

static inline int is_active_gate(struct gates *gates, gate_idx_t idx)
{
	return idx < gates->curr_size && gates->arr && gates->arr[idx] != NULL;
}

typedef struct snobj *
(*mod_cmd_func_t) (struct module *, const char *, struct snobj *);

size_t list_mclasses(const ModuleClass **p_arr, size_t arr_size,
		size_t offset);

const ModuleClass *find_mclass(const char *name);

int add_mclass(const ModuleClass *mclass);

#define ADD_MODULE(_MOD, _NAME_TEMPLATE, _HELP) \
	ModuleClassRegister<_MOD> noop(#_MOD, _NAME_TEMPLATE, _HELP);

#endif
