#ifndef BESS_MODULE_H_
#define BESS_MODULE_H_

#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gate.h"
#include "message.h"
#include "metadata.h"
#include "packet.h"
#include "scheduler.h"

using bess::gate_idx_t;

static inline void set_cmd_response_error(pb_cmd_response_t *response,
                                          const pb_error_t &error) {
  response->mutable_error()->CopyFrom(error);
}

#define MODULE_NAME_LEN 128
#define MAX_NUMA_NODE 16
#define UNCONSTRAINED_SOCKET ((0x1ull << MAX_NUMA_NODE) - 1)

struct task_result {
  uint64_t packets;
  uint64_t bits;
};

typedef uint16_t task_id_t;
typedef uint64_t placement_constraint;

#define MAX_TASKS_PER_MODULE 32
#define INVALID_TASK_ID ((task_id_t)-1)

using module_cmd_func_t =
    pb_func_t<pb_cmd_response_t, Module, google::protobuf::Any>;
using module_init_func_t = pb_func_t<pb_error_t, Module, google::protobuf::Any>;

template <typename T, typename M>
static inline module_cmd_func_t MODULE_CMD_FUNC(
    pb_cmd_response_t (M::*fn)(const T &)) {
  return [fn](Module *m, const google::protobuf::Any &arg) {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<M *>(m), arg_);
  };
}

template <typename T, typename M>
static inline module_init_func_t MODULE_INIT_FUNC(
    pb_error_t (M::*fn)(const T &)) {
  return [fn](Module *m, const google::protobuf::Any &arg) {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<M *>(m), arg_);
  };
}

class Module;

#define CALL_MEMBER_FN(obj, ptr_to_member_func) ((obj).*(ptr_to_member_func))

struct Command {
  std::string cmd;
  std::string arg_type;
  module_cmd_func_t func;
  // if non-zero, workers don't need to be paused in order to
  // run this command
  int mt_safe;
};

using Commands = std::vector<struct Command>;

// A class for generating new Modules of a particular type.
class ModuleBuilder {
 public:
  ModuleBuilder(
      std::function<Module *()> module_generator, const std::string &class_name,
      const std::string &name_template, const std::string &help_text,
      const gate_idx_t igates, const gate_idx_t ogates, const Commands &cmds,
      std::function<pb_error_t(Module *, const google::protobuf::Any &)>
          init_func)
      : module_generator_(module_generator),
        num_igates_(igates),
        num_ogates_(ogates),
        class_name_(class_name),
        name_template_(name_template),
        help_text_(help_text),
        cmds_(cmds),
        init_func_(init_func) {}

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

  static bool RegisterModuleClass(std::function<Module *()> module_generator,
                                  const std::string &class_name,
                                  const std::string &name_template,
                                  const std::string &help_text,
                                  const gate_idx_t igates,
                                  const gate_idx_t ogates, const Commands &cmds,
                                  module_init_func_t init_func);

  static bool DeregisterModuleClass(const std::string &class_name);

  static std::map<std::string, ModuleBuilder> &all_module_builders_holder(
      bool reset = false);
  static const std::map<std::string, ModuleBuilder> &all_module_builders();

  static const std::map<std::string, Module *> &all_modules();
  gate_idx_t NumIGates() const { return num_igates_; }
  gate_idx_t NumOGates() const { return num_ogates_; }

  const std::string &class_name() const { return class_name_; }
  const std::string &name_template() const { return name_template_; }
  const std::string &help_text() const { return help_text_; }

  const std::vector<std::pair<std::string, std::string>> cmds() const {
    std::vector<std::pair<std::string, std::string>> ret;
    for (auto &cmd : cmds_)
      ret.push_back(std::make_pair(cmd.cmd, cmd.arg_type));
    return ret;
  }

  static std::string GenerateDefaultName(const std::string &class_name,
                                         const std::string &default_template);

  pb_cmd_response_t RunCommand(Module *m, const std::string &user_cmd,
                               const google::protobuf::Any &arg) const;

  pb_error_t RunInit(Module *m, const google::protobuf::Any &arg) const;

 private:
  const std::function<Module *()> module_generator_;

  static std::map<std::string, Module *> all_modules_;

  const gate_idx_t num_igates_;
  const gate_idx_t num_ogates_;

  const std::string class_name_;
  const std::string name_template_;
  const std::string help_text_;
  const Commands cmds_;
  const module_init_func_t init_func_;
};

class ModuleTask;

/*!
 * Results from checking for constraints. Failing constraints can indicate
 * whether the failure is fatal or not.
 */
enum CheckConstraintResult {
  CHECK_OK = 0,
  CHECK_NONFATAL_ERROR = 1,
  CHECK_FATAL_ERROR = 2
};

class Module {
  // overide this section to create a new module -----------------------------
 public:
  Module()
      : name_(),
        module_builder_(),
        pipeline_(),
        attrs_(),
        attr_offsets_(),
        tasks_(),
        igates_(),
        ogates_(),
        active_workers_(),
        node_constraints_(UNCONSTRAINED_SOCKET),
        min_allowed_workers_(1),
        max_allowed_workers_(1) {}
  virtual ~Module() {}

  pb_error_t Init(const bess::pb::EmptyArg &arg);

  // NOTE: this function will be called even if Init() has failed.
  virtual void DeInit();

  virtual struct task_result RunTask(void *arg);
  virtual void ProcessBatch(bess::PacketBatch *batch);

  virtual std::string GetDesc() const { return ""; }
  virtual std::string GetDump() const { return ""; }

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands cmds;

  // -------------------------------------------------------------------------

 public:
  friend class ModuleBuilder;

  pb_error_t InitWithGenericArg(const google::protobuf::Any &arg);

  const ModuleBuilder *module_builder() const { return module_builder_; }

  bess::metadata::Pipeline *pipeline() const { return pipeline_; }

  const std::string &name() const { return name_; }

  /* Pass packets to the next module.
   * Packet deallocation is callee's responsibility. */
  inline void RunChooseModule(gate_idx_t ogate_idx, bess::PacketBatch *batch);

  /* Wrapper for single-output modules */
  inline void RunNextModule(bess::PacketBatch *batch);

  /*
   * Split a batch into several, one for each ogate
   * NOTE:
   *   1. Order is preserved for packets with the same gate.
   *   2. No ordering guarantee for packets with different gates.
   */
  void RunSplit(const gate_idx_t *ogates, bess::PacketBatch *mixed_batch);

  /* returns -errno if fails */
  int ConnectModules(gate_idx_t ogate_idx, Module *m_next,
                     gate_idx_t igate_idx);
  int DisconnectModulesUpstream(gate_idx_t igate_idx);
  int DisconnectModules(gate_idx_t ogate_idx);

  // Register a task.
  task_id_t RegisterTask(void *arg);

  /* Modules should call this function to declare additional metadata
   * attributes at initialization time.
   * Static metadata attributes that are defined in module class are
   * automatically registered, so only attributes specific to a module
   * 'instance'
   * need this function.
   * Returns its allocated ID (>= 0), or a negative number for error */
  int AddMetadataAttr(const std::string &name, size_t size,
                      bess::metadata::Attribute::AccessMode mode);

  int EnableTcpDump(const char *fifo, int is_igate, gate_idx_t gate_idx);

  int DisableTcpDump(int is_igate, gate_idx_t gate_idx);

  pb_cmd_response_t RunCommand(const std::string &cmd,
                               const google::protobuf::Any &arg) {
    return module_builder_->RunCommand(this, cmd, arg);
  }

  const std::vector<bess::metadata::Attribute> &all_attrs() const {
    return attrs_;
  }

  const std::vector<ModuleTask *> &tasks() const { return tasks_; }

  void set_attr_offset(size_t idx, bess::metadata::mt_offset_t offset) {
    if (idx < bess::metadata::kMaxAttrsPerModule) {
      attr_offsets_[idx] = offset;
    }
  }

  bess::metadata::mt_offset_t attr_offset(size_t idx) const {
    DCHECK_LT(idx, bess::metadata::kMaxAttrsPerModule);
    return attr_offsets_[idx];
  }

  const bess::metadata::mt_offset_t *all_attr_offsets() const {
    return attr_offsets_;
  }

  const std::vector<bess::IGate *> &igates() const { return igates_; }

  const std::vector<bess::OGate *> &ogates() const { return ogates_; }

  /*!
   * Compute placement constraints based on the current module and all
   * downstream modules (i.e., modules connected to out ports.
   */
  placement_constraint ComputePlacementConstraints() const;

  /*!
   * Reset the set of active workers.
   */
  void ResetActiveWorkerSet() { active_workers_.clear(); }

  const std::unordered_set<int> &active_workers() const {
    return active_workers_;
  }

  void AddActiveWorker(int wid);

  virtual CheckConstraintResult CheckModuleConstraints() const;

 private:
  void DestroyAllTasks();
  void DeregisterAllAttributes();

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

  std::vector<bess::metadata::Attribute> attrs_;
  bess::metadata::mt_offset_t attr_offsets_[bess::metadata::kMaxAttrsPerModule];

  std::vector<ModuleTask *> tasks_;

  std::vector<bess::IGate *> igates_;
  std::vector<bess::OGate *> ogates_;
  // Set of active workers accessing this module.
  std::unordered_set<int> active_workers_;

 protected:
  // TODO[apanda]: Move to some constraint structure?
  // Placement constraints for this module. We use this to update the task based
  // on all upstream tasks.
  placement_constraint node_constraints_;

  // The minimum number of workers that should be using this module.
  int min_allowed_workers_;

  // The maximum number of workers allowed to access this module. Should be set
  // to
  // greater than 1 iff the module is thread safe.
  int max_allowed_workers_;
  DISALLOW_COPY_AND_ASSIGN(Module);
};

static inline void deadend(bess::PacketBatch *batch) {
  ctx.incr_silent_drops(batch->cnt());
  bess::Packet::Free(batch);
}

inline void Module::RunChooseModule(gate_idx_t ogate_idx,
                                    bess::PacketBatch *batch) {
  bess::OGate *ogate;

  if (unlikely(batch->cnt() <= 0)) {
    return;
  }

  if (unlikely(ogate_idx >= ogates_.size())) {
    deadend(batch);
    return;
  }

  ogate = ogates_[ogate_idx];

  if (unlikely(!ogate)) {
    deadend(batch);
    return;
  }
  for (auto &hook : ogate->hooks()) {
    hook->ProcessBatch(batch);
  }

  for (auto &hook : ogate->igate()->hooks()) {
    hook->ProcessBatch(batch);
  }

  ctx.set_current_igate(ogate->igate_idx());
  (static_cast<Module *>(ogate->arg()))->ProcessBatch(batch);
}

inline void Module::RunNextModule(bess::PacketBatch *batch) {
  RunChooseModule(0, batch);
}

class Task;

namespace bess {
template <typename CallableTask>
class LeafTrafficClass;
}  // namespace bess

// Stores the arguments of a task created by a module.
class ModuleTask {
 public:
  // Doesn't take ownership of 'arg' and 'c'.  'c' can be null and it
  // can be changed later with SetTC()
  ModuleTask(void *arg, bess::LeafTrafficClass<Task> *c) : arg_(arg), c_(c) {}

  ~ModuleTask() {}

  void *arg() { return arg_; }

  void SetTC(bess::LeafTrafficClass<Task> *c) { c_ = c; }

  bess::LeafTrafficClass<Task> *GetTC() { return c_; }

 private:
  void *arg_;  // Auxiliary value passed to Module::RunTask().
  bess::LeafTrafficClass<Task> *c_;  // Leaf TC associated with this task.
};

// Functor used by a leaf in a Worker's Scheduler to run a task in a module.
class Task {
 public:
  // When this task is scheduled it will execute 'm' with 'arg'.
  // When the associated leaf is created/destroyed, 't' will be updated.
  Task(Module *m, void *arg, ModuleTask *t) : module_(m), arg_(arg), t_(t) {}

  // Called when the leaf that owns this task is destroyed.
  void Detach() {
    if (t_) {
      t_->SetTC(nullptr);
    }
  }

  // Called when the leaf that owns this task is created.
  void Attach(bess::LeafTrafficClass<Task> *c) {
    if (t_) {
      t_->SetTC(c);
    }
  }

  struct task_result operator()(void) {
    return module_->RunTask(arg_);
  }

  /*!
   * Compute constraints for the pipeline starting at this task.
   */
  placement_constraint GetSocketConstraints() const {
    if (module_) {
      return module_->ComputePlacementConstraints();
    } else {
      return UNCONSTRAINED_SOCKET;
    }
  }

  /*!
   * Add a worker to the set of workers that call this task.
   */
  void AddActiveWorker(int wid) {
    if (module_) {
      LOG(WARNING) << "Adding active worker for wid " << wid << " to "
                   << module_->name();
      module_->AddActiveWorker(wid);
    } else {
      LOG(WARNING) << "No module";
    }
  }

 private:
  // Used by operator().
  Module *module_;
  void *arg_;

  // Used to notify a module that a leaf is being created/destroyed.
  ModuleTask *t_;
};

/* run all per-thread initializers */
void init_module_worker(void);

#if SN_TRACE_MODULES
void _trace_before_call(Module *mod, Module *next, bess::PacketBatch *batch);

void _trace_after_call(void);
#endif

static inline gate_idx_t get_igate() {
  return ctx.current_igate();
}

template <typename T>
static inline int is_active_gate(const std::vector<T *> &gates,
                                 gate_idx_t idx) {
  return idx < gates.size() && gates.size() && gates[idx];
}

// Unsafe, but faster version. for offset use Attribute_offset().
template <typename T>
inline T *_ptr_attr_with_offset(bess::metadata::mt_offset_t offset,
                                bess::Packet *pkt) {
  promise(offset >= 0);
  uintptr_t addr = reinterpret_cast<uintptr_t>(pkt->metadata()) + offset;
  return reinterpret_cast<T *>(addr);
}

template <typename T>
inline T _get_attr_with_offset(bess::metadata::mt_offset_t offset,
                               bess::Packet *pkt) {
  return *_ptr_attr_with_offset<T>(offset, pkt);
}

template <typename T>
inline void _set_attr_with_offset(bess::metadata::mt_offset_t offset,
                                  bess::Packet *pkt, T val) {
  *(_ptr_attr_with_offset<T>(offset, pkt)) = val;
}

// Safe version.
template <typename T>
inline T *ptr_attr_with_offset(bess::metadata::mt_offset_t offset,
                               bess::Packet *pkt) {
  return bess::metadata::IsValidOffset(offset)
             ? _ptr_attr_with_offset<T>(offset, pkt)
             : nullptr;
}

template <typename T>
inline T get_attr_with_offset(bess::metadata::mt_offset_t offset,
                              bess::Packet *pkt) {
  return bess::metadata::IsValidOffset(offset)
             ? _get_attr_with_offset<T>(offset, pkt)
             : T();
}

template <typename T>
inline void set_attr_with_offset(bess::metadata::mt_offset_t offset,
                                 bess::Packet *pkt, T val) {
  if (bess::metadata::IsValidOffset(offset)) {
    _set_attr_with_offset<T>(offset, pkt, val);
  }
}

// Slowest but easiest.
// TODO(melvin): These ought to be members of Module
template <typename T>
inline T *ptr_attr(Module *m, int attr_id, bess::Packet *pkt) {
  return ptr_attr_with_offset<T>(m->attr_offset(attr_id), pkt);
}

template <typename T>
inline T get_attr(Module *m, int attr_id, bess::Packet *pkt) {
  return get_attr_with_offset<T>(m->attr_offset(attr_id), pkt);
}

template <typename T>
inline void set_attr(Module *m, int attr_id, bess::Packet *pkt, T val) {
  set_attr_with_offset(m->attr_offset(attr_id), pkt, val);
}

/*!
 * Update information about what workers are accessing what model.
 */
void propagate_active_worker();

// Define some common versions of the above functions
#define INSTANTIATE_MT_FOR_TYPE(type)                                        \
  template type *_ptr_attr_with_offset(bess::metadata::mt_offset_t offset,   \
                                       bess::Packet *pkt);                   \
  template type _get_attr_with_offset(bess::metadata::mt_offset_t offset,    \
                                      bess::Packet *pkt);                    \
  template void _set_attr_with_offset(bess::metadata::mt_offset_t offset,    \
                                      bess::Packet *pkt, type val);          \
  template type *ptr_attr_with_offset(bess::metadata::mt_offset_t offset,    \
                                      bess::Packet *pkt);                    \
  template type get_attr_with_offset(bess::metadata::mt_offset_t offset,     \
                                     bess::Packet *pkt);                     \
  template void set_attr_with_offset(bess::metadata::mt_offset_t offset,     \
                                     bess::Packet *pkt, type val);           \
  template type *ptr_attr<type>(Module * m, int attr_id, bess::Packet *pkt); \
  template type get_attr<type>(Module * m, int attr_id, bess::Packet *pkt);  \
  template void set_attr<>(Module * m, int attr_id, bess::Packet *pkt,       \
                           type val);

INSTANTIATE_MT_FOR_TYPE(uint8_t)
INSTANTIATE_MT_FOR_TYPE(uint16_t)
INSTANTIATE_MT_FOR_TYPE(uint32_t)
INSTANTIATE_MT_FOR_TYPE(uint64_t)

#define DEF_MODULE(_MOD, _NAME_TEMPLATE, _HELP)                          \
  class _MOD##_class {                                                   \
   public:                                                               \
    _MOD##_class() {                                                     \
      ModuleBuilder::RegisterModuleClass(                                \
          std::function<Module *()>([]() { return new _MOD(); }), #_MOD, \
          _NAME_TEMPLATE, _HELP, _MOD::kNumIGates, _MOD::kNumOGates,     \
          _MOD::cmds, MODULE_INIT_FUNC(&_MOD::Init));                    \
    }                                                                    \
    ~_MOD##_class() { ModuleBuilder::DeregisterModuleClass(#_MOD); }     \
  };

#define ADD_MODULE(_MOD, _NAME_TEMPLATE, _HELP) \
  DEF_MODULE(_MOD, _NAME_TEMPLATE, _HELP);      \
  static _MOD##_class _MOD##_singleton;

#endif  // BESS_MODULE_H_
