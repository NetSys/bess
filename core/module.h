#ifndef BESS_MODULE_H_
#define BESS_MODULE_H_

#include <map>
#include <string>
#include <vector>

#include "message.h"
#include "metadata.h"
#include "snbuf.h"
#include "snobj.h"
#include "task.h"
#include "utils/cdlist.h"
#include "utils/simd.h"

static inline void set_cmd_response_error(pb_cmd_response_t *response,
                                          const pb_error_t &error) {
  response->mutable_error()->CopyFrom(error);
}

typedef uint16_t gate_idx_t;

#define INVALID_GATE UINT16_MAX

/* A module may have up to MAX_GATES input/output gates (separately). */
#define MAX_GATES 8192
#define DROP_GATE MAX_GATES
static_assert(MAX_GATES < INVALID_GATE, "invalid macro value");
static_assert(DROP_GATE <= MAX_GATES, "invalid macro value");

#define MODULE_NAME_LEN 128

#define TRACK_GATES 1
#define TCPDUMP_GATES 1

#define MAX_TASKS_PER_MODULE 32
#define INVALID_TASK_ID ((task_id_t)-1)
#define MODULE_FUNC (struct snobj * (Module::*)(struct snobj *))

using module_cmd_func_t = pb_func_t<pb_cmd_response_t, Module>;
using module_init_func_t = pb_func_t<pb_error_t, Module>;

template <typename T, typename M>
static inline module_cmd_func_t MODULE_CMD_FUNC(
    pb_cmd_response_t (M::*fn)(const T &)) {
  return [=](Module *m, google::protobuf::Any arg) -> pb_cmd_response_t {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn =
        reinterpret_cast<pb_cmd_response_t (Module::*)(const T &)>(fn);
    return (*m.*(base_fn))(arg_);
  };
}

template <typename T, typename M>
static inline module_init_func_t MODULE_INIT_FUNC(
    pb_error_t (M::*fn)(const T &)) {
  return [=](Module *m, google::protobuf::Any arg) -> pb_error_t {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = reinterpret_cast<pb_error_t (Module::*)(const T &)>(fn);
    return (*m.*(base_fn))(arg_);
  };
}

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
   * Unconnected elements are filled with nullptr */
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

// TODO: Change type name to Command when removing snobj
struct PbCommand {
  std::string cmd;
  module_cmd_func_t func;
  // if non-zero, workers don't need to be paused in order to
  // run this command
  int mt_safe;
};

using PbCommands = std::vector<struct PbCommand>;

// A class for generating new Modules of a particular type.
class ModuleBuilder {
 public:
  ModuleBuilder(
      std::function<Module *()> module_generator, const std::string &class_name,
      const std::string &name_template, const std::string &help_text,
      const gate_idx_t igates, const gate_idx_t ogates,
      const Commands<Module> &cmds, const PbCommands &pb_cmds,
      std::function<pb_error_t(Module *, const google::protobuf::Any &)>
          init_func)
      : module_generator_(module_generator),
        class_name_(class_name),
        name_template_(name_template),
        help_text_(help_text),
        cmds_(cmds),
        pb_cmds_(pb_cmds),
        init_func_(init_func) {
    kNumIGates = igates;
    kNumOGates = ogates;
  }

  /* returns a pointer to the created module.
   * if error, returns nullptr and *perr is set */
  Module *CreateModule(const std::string &name,
                       bess::metadata::Pipeline *pipeline) const;

  // Add a module to the collection. Returns true on success, false otherwise.
  static bool AddModule(Module *m);

  // Remove a module from the collection. Returns 0 on success, -errno
  // otherwise.
  static int DestroyModule(Module *m, bool erase = true);
  static void DestroyAllModules();

  static bool RegisterModuleClass(
      std::function<Module *()> module_generator, const std::string &class_name,
      const std::string &name_template, const std::string &help_text,
      const gate_idx_t igates, const gate_idx_t ogates,
      const Commands<Module> &cmds, const PbCommands &pb_cmds,
      module_init_func_t init_func);

  static std::map<std::string, ModuleBuilder> &all_module_builders_holder(
      bool reset = false);
  static const std::map<std::string, ModuleBuilder> &all_module_builders();

  static const std::map<std::string, Module *> &all_modules();

  gate_idx_t NumIGates() const { return kNumIGates; }
  gate_idx_t NumOGates() const { return kNumOGates; }

  const std::string &class_name() const { return class_name_; };
  const std::string &name_template() const { return name_template_; };
  const std::string &help_text() const { return help_text_; };
  const std::vector<std::string> cmds() const {
    std::vector<std::string> ret;
    for (auto &cmd : cmds_)
      ret.push_back(cmd.cmd);
    return ret;
  }

  const std::vector<std::string> pb_cmds() const {
    std::vector<std::string> ret;
    for (auto &cmd : pb_cmds_)
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

  pb_cmd_response_t RunCommand(Module *m, const std::string &user_cmd,
                               const google::protobuf::Any &arg) const {
    for (auto &cmd : pb_cmds_) {
      if (user_cmd == cmd.cmd) {
        return cmd.func(m, arg);
      }
    }
    pb_cmd_response_t response;
    set_cmd_response_error(
        &response, pb_error(ENOTSUP, "'%s' does not support command '%s'",
                            class_name_.c_str(), user_cmd.c_str()));
    return response;
  }

  pb_error_t RunInit(Module *m, const google::protobuf::Any &arg) const {
    return init_func_(m, arg);
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
  PbCommands pb_cmds_;
  module_init_func_t init_func_;
};

class Module {
  // overide this section to create a new module -----------------------------
 public:
  Module() = default;
  virtual ~Module(){};

  pb_error_t Init(const google::protobuf::Any &arg);

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::EmptyArg &arg);

  virtual void Deinit() {}

  virtual struct task_result RunTask(void *arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc() const { return ""; };
  virtual struct snobj *GetDump() const { return snobj_nil(); }

  // -------------------------------------------------------------------------

 public:
  friend class ModuleBuilder;

  const ModuleBuilder *module_builder() const { return module_builder_; }

  bess::metadata::Pipeline *pipeline() const { return pipeline_; }

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
                      bess::metadata::AccessMode mode);

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

  pb_cmd_response_t RunCommand(const std::string &cmd,
                               const google::protobuf::Any &arg) {
    return module_builder_->RunCommand(this, cmd, arg);
  }

 private:
  void set_name(const std::string &name) { name_ = name; }
  void set_module_builder(const ModuleBuilder *builder) {
    module_builder_ = builder;
  }
  void set_pipeline(bess::metadata::Pipeline *pipeline) {
    pipeline_ = pipeline;
  }

  std::string name_;

  const ModuleBuilder *module_builder_;

  bess::metadata::Pipeline *pipeline_;

  DISALLOW_COPY_AND_ASSIGN(Module);

  // FIXME: porting in progress ----------------------------
 public:
  struct task *tasks[MAX_TASKS_PER_MODULE] = {};

  size_t num_attrs = 0;
  struct bess::metadata::mt_attr attrs[bess::metadata::kMaxAttrsPerModule] = {};

  int curr_scope = 0;

  bess::metadata::mt_offset_t attr_offsets[bess::metadata::kMaxAttrsPerModule] =
      {};
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

static inline int is_valid_attr(const std::string &name, size_t size,
                                bess::metadata::AccessMode mode) {
  if (name.empty())
    return 0;

  if (size < 1 || size > bess::metadata::kMetadataAttrMaxSize)
    return 0;

  if (mode != bess::metadata::AccessMode::READ &&
      mode != bess::metadata::AccessMode::WRITE &&
      mode != bess::metadata::AccessMode::UPDATE)
    return 0;

  return 1;
}

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
  return idx < gates->curr_size && gates->arr && gates->arr[idx] != nullptr;
}

typedef struct snobj *(*mod_cmd_func_t)(struct module *, const char *,
                                        struct snobj *);

// Unsafe, but faster version. for offset use mt_attr_offset().
template <typename T>
inline T *_ptr_attr_with_offset(bess::metadata::mt_offset_t offset,
                                struct snbuf *pkt) {
  promise(offset >= 0);
  uintptr_t addr = (uintptr_t)(pkt->_metadata + offset);
  return reinterpret_cast<T *>(addr);
}

template <typename T>
inline T _get_attr_with_offset(bess::metadata::mt_offset_t offset,
                               struct snbuf *pkt) {
  return *_ptr_attr_with_offset<T>(offset, pkt);
}

template <typename T>
inline void _set_attr_with_offset(bess::metadata::mt_offset_t offset,
                                  struct snbuf *pkt, T val) {
  *(_ptr_attr_with_offset<T>(offset, pkt)) = val;
}

// Safe version.
template <typename T>
inline T *ptr_attr_with_offset(bess::metadata::mt_offset_t offset,
                               struct snbuf *pkt) {
  return bess::metadata::IsValidOffset(offset)
             ? _ptr_attr_with_offset<T>(offset, pkt)
             : nullptr;
}

template <typename T>
inline T get_attr_with_offset(bess::metadata::mt_offset_t offset,
                              struct snbuf *pkt) {
  T _zeroed = {};
  return bess::metadata::IsValidOffset(offset)
             ? _get_attr_with_offset<T>(offset, pkt)
             : _zeroed;
}

template <typename T>
inline void set_attr_with_offset(bess::metadata::mt_offset_t offset,
                                 struct snbuf *pkt, T val) {
  if (bess::metadata::IsValidOffset(offset)) {
    _set_attr_with_offset<T>(offset, pkt, val);
  }
}

// Slowest but easiest.
// TODO(melvin): These ought to be members of Module
template <typename T>
inline T *ptr_attr(Module *m, int attr_id, struct snbuf *pkt) {
  return ptr_attr_with_offset<T>(m->attr_offsets[attr_id], pkt);
}

template <typename T>
inline T get_attr(Module *m, int attr_id, struct snbuf *pkt) {
  return get_attr_with_offset<T>(m->attr_offsets[attr_id], pkt);
}

template <typename T>
inline void set_attr(Module *m, int attr_id, struct snbuf *pkt, T val) {
  set_attr_with_offset(m->attr_offsets[attr_id], pkt, val);
}

// Define some common versions of the above functions
#define INSTANTIATE_MT_FOR_TYPE(type)                                        \
  template type *_ptr_attr_with_offset(bess::metadata::mt_offset_t offset,   \
                                       struct snbuf *pkt);                   \
  template type _get_attr_with_offset(bess::metadata::mt_offset_t offset,    \
                                      struct snbuf *pkt);                    \
  template void _set_attr_with_offset(bess::metadata::mt_offset_t offset,    \
                                      struct snbuf *pkt, type val);          \
  template type *ptr_attr_with_offset(bess::metadata::mt_offset_t offset,    \
                                      struct snbuf *pkt);                    \
  template type get_attr_with_offset(bess::metadata::mt_offset_t offset,     \
                                     struct snbuf *pkt);                     \
  template void set_attr_with_offset(bess::metadata::mt_offset_t offset,     \
                                     struct snbuf *pkt, type val);           \
  template type *ptr_attr<type>(Module * m, int attr_id, struct snbuf *pkt); \
  template type get_attr<type>(Module * m, int attr_id, struct snbuf *pkt);  \
  template void set_attr<>(Module * m, int attr_id, struct snbuf *pkt,       \
                           type val);

INSTANTIATE_MT_FOR_TYPE(uint8_t)
INSTANTIATE_MT_FOR_TYPE(uint16_t)
INSTANTIATE_MT_FOR_TYPE(uint32_t)
INSTANTIATE_MT_FOR_TYPE(uint64_t)

#define ADD_MODULE(_MOD, _NAME_TEMPLATE, _HELP)                              \
  bool __module__##_MOD = ModuleBuilder::RegisterModuleClass(                \
      std::function<Module *()>([]() { return new _MOD(); }), #_MOD,         \
      _NAME_TEMPLATE, _HELP, _MOD::kNumIGates, _MOD::kNumOGates, _MOD::cmds, \
      _MOD::pb_cmds, MODULE_INIT_FUNC(&_MOD::InitPb));

#endif  // BESS_MODULE_H_
