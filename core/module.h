#ifndef _MODULE_H_
#define _MODULE_H_

#include <cassert>
#include <map>
#include <string>
#include <vector>

#include "common.h"
#include "log.h"
#include "message.h"

#include <glog/logging.h>

typedef uint16_t task_id_t;
typedef uint16_t gate_idx_t;

#define INVALID_GATE UINT16_MAX

/* A module may have up to MAX_GATES input/output gates (separately). */
#define MAX_GATES 8192
#define DROP_GATE MAX_GATES
static_assert(MAX_GATES < INVALID_GATE, "invalid macro value");
static_assert(DROP_GATE <= MAX_GATES, "invalid macro value");

#include "bessctl.grpc.pb.h"
#include "metadata.h"
#include "snbuf.h"
#include "snobj.h"
#include "utils/cdlist.h"
#include "utils/simd.h"
#include "worker.h"

using bess::Error;

#define MODULE_NAME_LEN 128

#define TRACK_GATES 1
#define TCPDUMP_GATES 1

#define MAX_TASKS_PER_MODULE 32
#define INVALID_TASK_ID ((task_id_t)-1)
#define MODULE_FUNC (struct snobj * (Module::*)(struct snobj *))

struct task_result {
  uint64_t packets;
  uint64_t bits;
};

class Module;

struct gate {
  /* immutable values */
  Module *m;           /* the module this gate belongs to */
  gate_idx_t gate_idx; /* input/output gate index of itself */

  /* mutable values below */
  void *arg;

  union {
    struct {
      struct cdlist_item igate_upstream;
      struct gate *igate;
      gate_idx_t igate_idx; /* cache for igate->gate_idx */
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

template <typename T>
struct Command {
  std::string cmd;
  struct snobj *(T::*func)(struct snobj *);

  // if non-zero, workers don't need to be paused in order to
  // run this command
  int mt_safe;
};

template <typename T>
using Commands = std::vector<struct Command<T> >;

struct task {
  struct tc *c;

  Module *m;
  void *arg;

  struct cdlist_item tc;
  struct cdlist_item all_tasks;
};

// A class for generating new Modules of a particular type.
class ModuleBuilder {
 public:
  ModuleBuilder(std::function<Module *()> module_generator,
                const std::string &class_name, const std::string &name_template,
                const std::string &help_text, const gate_idx_t igates,
                const gate_idx_t ogates, const Commands<Module> &cmds)
      : module_generator_(module_generator),
        class_name_(class_name),
        name_template_(name_template),
        help_text_(help_text),
        cmds_(cmds) {
    kNumIGates = igates;
    kNumOGates = ogates;
  }

  /* returns a pointer to the created module.
   * if error, returns NULL and *perr is set */
  Module *CreateModule(const std::string &name) const;

  // Add a module to the collection. Returns true on success, false otherwise.
  static bool AddModule(Module *m);

  // Remove a module from the collection. Returns 0 on success, -errno
  // otherwise.
  static int DestroyModule(Module *m, bool erase = true);
  static void DestroyAllModules();

  static bool RegisterModuleClass(std::function<Module *()> module_generator,
                                  const std::string &class_name,
                                  const std::string &name_template,
                                  const std::string &help_text,
                                  const gate_idx_t igates,
                                  const gate_idx_t ogates,
                                  const Commands<Module> &cmds);

  static std::map<std::string, ModuleBuilder> &all_module_builders_holder(
      bool reset = false);
  static const std::map<std::string, ModuleBuilder> &all_module_builders();

  static const std::map<std::string, Module *> &all_modules();

  const gate_idx_t NumIGates() const { return kNumIGates; }
  const gate_idx_t NumOGates() const { return kNumOGates; }

  const std::string &class_name() const { return class_name_; };
  const std::string &name_template() const { return name_template_; };
  const std::string &help_text() const { return help_text_; };
  const std::vector<std::string> cmds() const {
    std::vector<std::string> ret;
    for (auto &cmd : cmds_)
      ret.push_back(cmd.cmd);
    return ret;
  }

  static std::string GenerateDefaultName(const std::string &class_name,
                                         const std::string &default_template);

  struct snobj *RunCommand(Module *m, const std::string &user_cmd,
                           struct snobj *arg) const {
    for (auto &cmd : cmds_) {
      if (user_cmd == cmd.cmd)
        return (*m.*(cmd.func))(arg);
    }

    return snobj_err(ENOTSUP, "'%s' does not support command '%s'",
                     class_name_.c_str(), user_cmd.c_str());
  }

 private:
  std::function<Module *()> module_generator_;

  static std::map<std::string, ModuleBuilder> &all_module_builders_;
  static std::map<std::string, Module *> all_modules_;

  gate_idx_t kNumIGates;
  gate_idx_t kNumOGates;

  std::string class_name_;
  std::string name_template_;
  std::string help_text_;
  Commands<Module> cmds_;
};

class Module {
  // overide this section to create a new module -----------------------------
 public:
  Module() = default;
  virtual ~Module(){};

  virtual bess::Error *Init(const void *arg) { return nullptr; }
  virtual struct snobj *Init(struct snobj *arg) { return nullptr; }
  virtual void Deinit() {}

  virtual struct task_result RunTask(void *arg) { assert(0); }
  virtual void ProcessBatch(struct pkt_batch *batch) { assert(0); }

  virtual std::string GetDesc() const { return ""; };
  virtual struct snobj *GetDump() const { return snobj_nil(); }

  // -------------------------------------------------------------------------

 public:
  friend class ModuleBuilder;

  const ModuleBuilder *module_builder() const { return module_builder_; }

  const std::string &name() const { return name_; }

  /* Pass packets to the next module.
   * Packet deallocation is callee's responsibility. */
  inline void RunChooseModule(gate_idx_t ogate_idx, struct pkt_batch *batch);

  /* Wrapper for single-output modules */
  inline void RunNextModule(struct pkt_batch *batch);

  /*
   * Split a batch into several, one for each ogate
   * NOTE:
   *   1. Order is preserved for packets with the same gate.
   *   2. No ordering guarantee for packets with different gates.
   */
  void RunSplit(const gate_idx_t *ogates, struct pkt_batch *mixed_batch);

  /* returns -errno if fails */
  int ConnectModules(gate_idx_t ogate_idx, Module *m_next,
                     gate_idx_t igate_idx);
  int DisconnectModulesUpstream(gate_idx_t igate_idx);
  int DisconnectModules(gate_idx_t ogate_idx);
  int GrowGates(struct gates *gates, gate_idx_t gate);

  int NumTasks();
  task_id_t RegisterTask(void *arg);
  void DestroyAllTasks();

  /* Modules should call this function to declare additional metadata
   * attributes at initialization time.
   * Static metadata attributes that are defined in module class are
   * automatically registered, so only attributes specific to a module
   * 'instance'
   * need this function.
   * Returns its allocated ID (>= 0), or a negative number for error */
  int AddMetadataAttr(const std::string &name, int size,
                      enum mt_access_mode mode);

#if TCPDUMP_GATES
  int EnableTcpDump(const char *fifo, gate_idx_t gate);

  int DisableTcpDump(gate_idx_t gate);

  void DumpPcapPkts(struct gate *gate, struct pkt_batch *batch);
#else
  /* Cannot enable tcpdump */
  inline int enable_tcpdump(const char *, gate_idx_t) { return -EINVAL; }

  /* Cannot disable tcpdump */
  inline int disable_tcpdump(Module *, int) { return -EINVAL; }
#endif

  struct snobj *RunCommand(const std::string &cmd, struct snobj *arg) {
    return module_builder_->RunCommand(this, cmd, arg);
  }

 private:
  void set_name(const std::string &name) { name_ = name; }
  void set_module_builder(const ModuleBuilder *builder) {
    module_builder_ = builder;
  }

  std::string name_;

  const ModuleBuilder *module_builder_;

  DISALLOW_COPY_AND_ASSIGN(Module);

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

void deadend(struct pkt_batch *batch);

inline void Module::RunChooseModule(gate_idx_t ogate_idx,
                                    struct pkt_batch *batch) {
  struct gate *ogate;

  if (unlikely(ogate_idx >= ogates.curr_size)) {
    deadend(batch);
    return;
  }

  ogate = ogates.arr[ogate_idx];

  if (unlikely(!ogate)) {
    deadend(batch);
    return;
  }

#if SN_TRACE_MODULES
  _trace_before_call(this, next, batch);
#endif

#if TRACK_GATES
  ogate->cnt += 1;
  ogate->pkts += batch->cnt;
#endif

#if TCPDUMP_GATES
  if (unlikely(ogate->tcpdump))
    DumpPcapPkts(ogate, batch);
#endif

  ctx.push_igate(ogate->out.igate_idx);

  // XXX
  ((Module *)ogate->arg)->ProcessBatch(batch);

  ctx.pop_igate();

#if SN_TRACE_MODULES
  _trace_after_call();
#endif
}

inline void Module::RunNextModule(struct pkt_batch *batch) {
  RunChooseModule(0, batch);
}

static int is_valid_attr(const std::string &name, int size,
                         enum mt_access_mode mode) {
  if (name.empty())
    return 0;

  if (size < 1 || size > MT_ATTR_MAX_SIZE)
    return 0;

  if (mode != MT_READ && mode != MT_WRITE && mode != MT_UPDATE)
    return 0;

  return 1;
}

struct task *task_create(Module *m, void *arg);

void task_destroy(struct task *t);

static inline int task_is_attached(struct task *t) {
  return (t->c != NULL);
}

void task_attach(struct task *t, struct tc *c);
void task_detach(struct task *t);

static inline struct task_result task_scheduled(struct task *t) {
  return t->m->RunTask(t->arg);
}

void assign_default_tc(int wid, struct task *t);
void process_orphan_tasks();

task_id_t task_to_tid(struct task *t);

/* run all per-thread initializers */
void init_module_worker(void);

#if SN_TRACE_MODULES
void _trace_before_call(Module *mod, Module *next, struct pkt_batch *batch);

void _trace_after_call(void);
#endif

static inline gate_idx_t get_igate() {
  return ctx.igate_stack_top();
}

static inline int is_active_gate(struct gates *gates, gate_idx_t idx) {
  return idx < gates->curr_size && gates->arr && gates->arr[idx] != NULL;
}

typedef struct snobj *(*mod_cmd_func_t)(struct module *, const char *,
                                        struct snobj *);

#define ADD_MODULE(_MOD, _NAME_TEMPLATE, _HELP)                      \
  bool __module__##_MOD = ModuleBuilder::RegisterModuleClass(        \
      std::function<Module *()>([]() { return new _MOD(); }), #_MOD, \
      _NAME_TEMPLATE, _HELP, _MOD::kNumIGates, _MOD::kNumOGates, _MOD::cmds);

#endif
