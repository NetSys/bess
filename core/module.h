#ifndef BESS_MODULE_H_
#define BESS_MODULE_H_

#include <map>
#include <string>
#include <vector>

#include "gate.h"
#include "message.h"
#include "metadata.h"
#include "packet.h"
#include "task.h"

static inline void set_cmd_response_error(pb_cmd_response_t *response,
                                          const pb_error_t &error) {
  response->mutable_error()->CopyFrom(error);
}
#define MODULE_NAME_LEN 128

#define MAX_TASKS_PER_MODULE 32
#define INVALID_TASK_ID ((task_id_t)-1)

using module_cmd_func_t =
    pb_func_t<pb_cmd_response_t, Module, google::protobuf::Any>;
using module_init_func_t = pb_func_t<pb_error_t, Module, google::protobuf::Any>;

template <typename T, typename M>
static inline module_cmd_func_t MODULE_CMD_FUNC(
    pb_cmd_response_t (M::*fn)(const T &)) {
  return [=](Module *m, const google::protobuf::Any &arg) -> pb_cmd_response_t {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<M *>(m), arg_);
  };
}

template <typename T, typename M>
static inline module_init_func_t MODULE_INIT_FUNC(
    pb_error_t (M::*fn)(const T &)) {
  return [=](Module *m, const google::protobuf::Any &arg) -> pb_error_t {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<M *>(m), arg_);
  };
}

class Module;

#define CALL_MEMBER_FN(obj, ptr_to_member_func) ((obj).*(ptr_to_member_func))

template <typename T>
struct Command {
  std::string cmd;

  // if non-zero, workers don't need to be paused in order to
  // run this command
  int mt_safe;
};

template <typename T>
using Commands = std::vector<struct Command<T>>;

// TODO: Change type name to Command when removing snobj
struct PbCommand {
  std::string cmd;
  std::string arg_type;
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
      const PbCommands &pb_cmds,
      std::function<pb_error_t(Module *, const google::protobuf::Any &)>
          init_func)
      : module_generator_(module_generator),
        num_igates_(igates),
        num_ogates_(ogates),
        class_name_(class_name),
        name_template_(name_template),
        help_text_(help_text),
        pb_cmds_(pb_cmds),
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

  static bool RegisterModuleClass(
      std::function<Module *()> module_generator, const std::string &class_name,
      const std::string &name_template, const std::string &help_text,
      const gate_idx_t igates, const gate_idx_t ogates,
      const PbCommands &pb_cmds, module_init_func_t init_func);

  static std::map<std::string, ModuleBuilder> &all_module_builders_holder(
      bool reset = false);
  static const std::map<std::string, ModuleBuilder> &all_module_builders();

  static const std::map<std::string, Module *> &all_modules();

  gate_idx_t NumIGates() const { return num_igates_; }
  gate_idx_t NumOGates() const { return num_ogates_; }

  const std::string &class_name() const { return class_name_; };
  const std::string &name_template() const { return name_template_; };
  const std::string &help_text() const { return help_text_; };

  const std::vector<std::pair<std::string, std::string>> pb_cmds() const {
    std::vector<std::pair<std::string, std::string>> ret;
    for (auto &cmd : pb_cmds_)
      ret.push_back(std::make_pair(cmd.cmd, cmd.arg_type));
    return ret;
  }

  static std::string GenerateDefaultName(const std::string &class_name,
                                         const std::string &default_template);

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
  const std::function<Module *()> module_generator_;

  static std::map<std::string, Module *> all_modules_;

  const gate_idx_t num_igates_;
  const gate_idx_t num_ogates_;

  const std::string class_name_;
  const std::string name_template_;
  const std::string help_text_;
  const PbCommands pb_cmds_;
  const module_init_func_t init_func_;
};

class Module {
  // overide this section to create a new module -----------------------------
 public:
  Module()
      : name_(),
        module_builder_(),
        pipeline_(),
        attrs_(),
        tasks(),
        attr_offsets(),
        igates(),
        ogates() {}
  virtual ~Module() {}

  pb_error_t Init(const google::protobuf::Any &arg);

  pb_error_t InitPb(const bess::pb::EmptyArg &arg);

  virtual void Deinit() {}

  virtual struct task_result RunTask(void *arg);
  virtual void ProcessBatch(bess::PacketBatch *batch);

  virtual std::string GetDesc() const { return ""; };
  virtual std::string GetDump() const { return ""; }

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const PbCommands pb_cmds;

  // -------------------------------------------------------------------------

 public:
  friend class ModuleBuilder;

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

  int NumTasks();
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

  DISALLOW_COPY_AND_ASSIGN(Module);

  // FIXME: porting in progress ----------------------------
 public:
  struct task *tasks[MAX_TASKS_PER_MODULE];

  bess::metadata::mt_offset_t attr_offsets[bess::metadata::kMaxAttrsPerModule];

  std::vector<bess::IGate *> igates;
  std::vector<bess::OGate *> ogates;
};

void deadend(bess::PacketBatch *batch);

inline void Module::RunChooseModule(gate_idx_t ogate_idx,
                                    bess::PacketBatch *batch) {
  bess::Gate *ogate;

  if (unlikely(ogate_idx >= ogates.size())) {
    deadend(batch);
    return;
  }

  ogate = ogates[ogate_idx];

  if (unlikely(!ogate)) {
    deadend(batch);
    return;
  }

  // Place packets into buffer so they can be run later
  if (!ctx.push_ogate_and_packets(ogate, batch)) {
    // This really shouldn't happen.
    deadend(batch);
    return;
  }
}

inline void Module::RunNextModule(bess::PacketBatch *batch) {
  RunChooseModule(0, batch);
}

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
  return ptr_attr_with_offset<T>(m->attr_offsets[attr_id], pkt);
}

template <typename T>
inline T get_attr(Module *m, int attr_id, bess::Packet *pkt) {
  return get_attr_with_offset<T>(m->attr_offsets[attr_id], pkt);
}

template <typename T>
inline void set_attr(Module *m, int attr_id, bess::Packet *pkt, T val) {
  set_attr_with_offset(m->attr_offsets[attr_id], pkt, val);
}

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

#define ADD_MODULE(_MOD, _NAME_TEMPLATE, _HELP)                      \
  bool __module__##_MOD = ModuleBuilder::RegisterModuleClass(        \
      std::function<Module *()>([]() { return new _MOD(); }), #_MOD, \
      _NAME_TEMPLATE, _HELP, _MOD::kNumIGates, _MOD::kNumOGates,     \
      _MOD::pb_cmds, MODULE_INIT_FUNC(&_MOD::InitPb));

#endif  // BESS_MODULE_H_
