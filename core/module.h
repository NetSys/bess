// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_MODULE_H_
#define BESS_MODULE_H_

#include <array>
#include <atomic>
#include <map>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "commands.h"
#include "event.h"
#include "gate.h"
#include "message.h"
#include "metadata.h"
#include "packet_pool.h"
#include "worker.h"

using bess::gate_idx_t;

#define INVALID_TASK_ID ((task_id_t)-1)
#define MAX_NUMA_NODE 16
#define MAX_TASKS_PER_MODULE 32
#define UNCONSTRAINED_SOCKET ((0x1ull << MAX_NUMA_NODE) - 1)

struct Context {
  // Set by task scheduler, read by modules
  uint64_t current_tsc;
  uint64_t current_ns;
  int wid;
  Task *task;

  // Set by module scheduler, read by a task scheduler
  uint64_t silent_drops;

  // Temporary variables to be accessed and updated by module scheduler
  gate_idx_t current_igate;
  int gate_with_hook_cnt = 0;
  int gate_without_hook_cnt = 0;
  gate_idx_t gate_with_hook[bess::PacketBatch::kMaxBurst];
  gate_idx_t gate_without_hook[bess::PacketBatch::kMaxBurst];
};

using module_cmd_func_t =
    pb_func_t<CommandResponse, Module, google::protobuf::Any>;
using module_init_func_t =
    pb_func_t<CommandResponse, Module, google::protobuf::Any>;

template <typename T, typename M>
static inline module_cmd_func_t MODULE_CMD_FUNC(
    CommandResponse (M::*fn)(const T &)) {
  return [fn](Module *m, const google::protobuf::Any &arg) {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<M *>(m), arg_);
  };
}

template <typename T, typename M>
static inline module_init_func_t MODULE_INIT_FUNC(
    CommandResponse (M::*fn)(const T &)) {
  return [fn](Module *m, const google::protobuf::Any &arg) {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<M *>(m), arg_);
  };
}

class Module;

// A class for managing modules of 'a particular type'.
// Creates new modules and forwards module-specific commands.
class ModuleBuilder {
 public:
  ModuleBuilder(
      std::function<Module *()> module_generator, const std::string &class_name,
      const std::string &name_template, const std::string &help_text,
      const gate_idx_t igates, const gate_idx_t ogates, const Commands &cmds,
      std::function<CommandResponse(Module *, const google::protobuf::Any &)>
          init_func)
      : module_generator_(module_generator),
        num_igates_(igates),
        num_ogates_(ogates),
        class_name_(class_name),
        name_template_(name_template),
        help_text_(help_text),
        cmds_(cmds),
        init_func_(init_func) {}

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

  // returns a pointer to the created module
  Module *CreateModule(const std::string &name,
                       bess::metadata::Pipeline *pipeline) const;

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

  CommandResponse RunCommand(Module *m, const std::string &user_cmd,
                             const google::protobuf::Any &arg) const;

  CommandResponse RunInit(Module *m, const google::protobuf::Any &arg) const;

 private:
  const std::function<Module *()> module_generator_;

  const gate_idx_t num_igates_;
  const gate_idx_t num_ogates_;

  const std::string class_name_;
  const std::string name_template_;
  const std::string help_text_;
  const Commands cmds_;
  const module_init_func_t init_func_;
};

class Task;

// Results from checking for constraints. Failing constraints can indicate
// whether the failure is fatal or not.
enum CheckConstraintResult {
  CHECK_OK = 0,
  CHECK_NONFATAL_ERROR = 1,
  CHECK_FATAL_ERROR = 2
};

class alignas(64) Module {
  // overide this section to create a new module -----------------------------
 public:
  Module()
      : name_(),
        module_builder_(),
        initial_arg_(),
        pipeline_(),
        attrs_(),
        attr_offsets_(),
        tasks_(),
        igates_(),
        ogates_(),
        deadends_(),
        active_workers_(Worker::kMaxWorkers, false),
        visited_tasks_(),
        is_task_(false),
        parent_tasks_(),
        children_overload_(0),
        overload_(false),
        node_constraints_(UNCONSTRAINED_SOCKET),
        min_allowed_workers_(1),
        max_allowed_workers_(1),
        propagate_workers_(true) {}
  virtual ~Module() {}

  CommandResponse Init(const bess::pb::EmptyArg &arg);

  // NOTE: this function will be called even if Init() has failed.
  virtual void DeInit();

  // Initiates a new task with 'ctx', generating a new workload (a set of
  // packets in 'batch') and forward the workloads to be processed and forward
  // the workload to next modules. It can also get per-module specific 'arg'
  // as input. 'batch' is pre-allocated for efficiency.
  // It returns info about generated workloads, 'task_result'.
  virtual struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                                     void *arg);

  // Process a set of packets in packet batch with the contexts 'ctx'.
  // A module should handle all packets in a batch properly as follows:
  // 1) forwards to the next modules, or 2) free
  virtual void ProcessBatch(Context *ctx, bess::PacketBatch *batch);

  // If a derived Module overrides OnEvent and doesn't return  -ENOTSUP for a
  // particular Event `e` it will be invoked for each instance of the derived
  // Module whenever `e` occours. See `event.h` for details about the various
  // event types and their semantics/requirements when it comes to modules.
  virtual int OnEvent(bess::Event) { return -ENOTSUP; }

  virtual std::string GetDesc() const { return ""; }

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands cmds;

  // -------------------------------------------------------------------------

 public:
  friend class ModuleBuilder;
  friend class ModuleGraph;

  CommandResponse InitWithGenericArg(const google::protobuf::Any &arg);

  // With the contexts('ctx'), pass packet batch ('batch') to the next module
  // connected with 'ogate_idx'
  inline void RunChooseModule(Context *ctx, gate_idx_t ogate_idx,
                              bess::PacketBatch *batch);

  // With the contexts('ctx'), pass packet batch ('batch') to the default
  // next module ('ogate_idx' == 0)
  inline void RunNextModule(Context *ctx, bess::PacketBatch *batch);

  // With the contexts('ctx'), drop a packet. Dropped packets will be freed.
  inline void DropPacket(Context *ctx, bess::Packet *pkt);

  // With the contexts('ctx'), emit (forward) a packet ('pkt') to the next
  // module connected with 'ogate'
  inline void EmitPacket(Context *ctx, bess::Packet *pkt, gate_idx_t ogate = 0);

  // Process OGate hooks and forward packet batches into next modules.
  inline void ProcessOGates(Context *ctx);

  /*
   * Split a batch into several, one for each ogate
   * NOTE:
   *   1. Order is preserved for packets with the same gate.
   *   2. No ordering guarantee for packets with different gates.
   *
   * Update on 11/27/2017, by Shinae Woo
   * This interface will become DEPRECATED.
   * Consider using new interfafces supporting faster data-plane support
   * DropPacket()/EmitPacket()
   * */
  [[deprecated(
      "use the new API EmitPacket()/DropPacket() instead")]] inline void
  RunSplit(Context *ctx, const gate_idx_t *ogates,
           bess::PacketBatch *mixed_batch);

  // Register a task.
  task_id_t RegisterTask(void *arg);

  // Modules should call this function to declare additional metadata
  // attributes at initialization time.
  // Static metadata attributes that are defined in module class are
  // automatically registered, so only attributes specific to a module
  // 'instance'
  // need this function.
  // Returns its allocated ID (>= 0), or a negative number for error */
  int AddMetadataAttr(const std::string &name, size_t size,
                      bess::metadata::Attribute::AccessMode mode);

  CommandResponse RunCommand(const std::string &cmd,
                             const google::protobuf::Any &arg) {
    return module_builder_->RunCommand(this, cmd, arg);
  }

  const ModuleBuilder *module_builder() const { return module_builder_; }

  bess::metadata::Pipeline *pipeline() const { return pipeline_; }

  const std::string &name() const { return name_; }

  const std::vector<bess::metadata::Attribute> &all_attrs() const {
    return attrs_;
  }

  bool is_task() const { return is_task_; }

  const std::vector<const Task *> &tasks() const { return tasks_; }

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

  uint64_t deadends() const {
    return std::accumulate(deadends_.begin(), deadends_.end(), 0);
  }

  // Compute placement constraints based on the current module and all
  // downstream modules (i.e., modules connected to out ports.
  placement_constraint ComputePlacementConstraints(
      std::unordered_set<const Module *> *visited) const;

  // Reset the set of active workers.
  void ResetActiveWorkerSet() {
    std::fill(active_workers_.begin(), active_workers_.end(), false);
    visited_tasks_.clear();
    deadends_.fill(0);
  }

  const std::vector<bool> &active_workers() const { return active_workers_; }

  // Number of active workers attached to this module.
  inline size_t num_active_workers() const {
    return std::count_if(active_workers_.begin(), active_workers_.end(),
                         [](bool b) { return b; });
  }

  // True if any worker attached to this module is running.
  inline bool HasRunningWorker() const {
    for (int wid = 0; wid < Worker::kMaxWorkers; wid++) {
      if (active_workers_[wid] && is_worker_running(wid)) {
        return true;
      }
    }
    return false;
  }

  // Check if we have already seen a task
  inline bool HaveVisitedWorker(const Task *task) const {
    return std::find(visited_tasks_.begin(), visited_tasks_.end(), task) !=
           visited_tasks_.end();
  }

  // Number of tasks that access this module
  inline size_t num_active_tasks() const { return visited_tasks_.size(); }

  const std::vector<Module *> &parent_tasks() const { return parent_tasks_; };

  virtual void AddActiveWorker(int wid, const Task *task);

  virtual CheckConstraintResult CheckModuleConstraints() const;

  // For testing.
  int children_overload() const { return children_overload_; };

  // Signals to parent task(s) that module is overloaded.
  // TODO: SignalOverload and SignalUnderload are only safe if the module is not
  // thread safe (e.g. multiple workers should not be able to simultaneously
  // call these methods)
  void SignalOverload() {
    if (overload_) {
      return;
    }
    for (auto const &p : parent_tasks_) {
      ++(p->children_overload_);
    }

    overload_ = true;
  }

  // Signals to parent task(s) that module is underloaded.
  void SignalUnderload() {
    if (!overload_) {
      return;
    }

    for (auto const &p : parent_tasks_) {
      --(p->children_overload_);
    }

    overload_ = false;
  }

 private:
  // Module Destory, connect, task managements are only available with
  // ModuleGraph class
  int ConnectGate(gate_idx_t ogate_idx, Module *m_next, gate_idx_t igate_idx);
  int DisconnectGate(gate_idx_t ogate_idx);
  void DisconnectModulesUpstream(gate_idx_t igate_idx);
  void DestroyAllTasks();
  void DeregisterAllAttributes();
  void AddParentTask(Module *task) { parent_tasks_.push_back(task); }
  void ClearParentTasks() { parent_tasks_.clear(); }

  // Destroy a module and cleaning up including
  // calling per-module Deinit() function,
  // disconnect from/to upstream/downstream modules,
  // destory all tasks if it is a task module
  // deregister all metadata attributes if it has
  void Destroy();

  void set_name(const std::string &name) { name_ = name; }
  void set_module_builder(const ModuleBuilder *builder) {
    module_builder_ = builder;
  }
  void set_pipeline(bess::metadata::Pipeline *pipeline) {
    pipeline_ = pipeline;
  }

  std::string name_;

  const ModuleBuilder *module_builder_;
  google::protobuf::Any initial_arg_;

  bess::metadata::Pipeline *pipeline_;

  std::vector<bess::metadata::Attribute> attrs_;
  bess::metadata::mt_offset_t attr_offsets_[bess::metadata::kMaxAttrsPerModule];

  std::vector<const Task *> tasks_;

  std::vector<bess::IGate *> igates_;
  std::vector<bess::OGate *> ogates_;
  std::array<uint64_t, Worker::kMaxWorkers> deadends_;

 protected:
  // Set of active workers accessing this module.
  std::vector<bool> active_workers_;
  // Set of tasks we have already accounted for when propagating workers.
  std::vector<const Task *> visited_tasks_;

  // Whether the module overrides RunTask or not.
  bool is_task_;

  // Parent tasks of this module in the current pipeline.
  std::vector<Module *> parent_tasks_;

  // # of child tasks of this module that are overloaded.
  std::atomic<int> children_overload_;

  // Whether the module itself is overloaded.
  bool overload_;

  // TODO[apanda]: Move to some constraint structure?
  // Placement constraints for this module. We use this to update the task based
  // on all upstream tasks.
  placement_constraint node_constraints_;

  // The minimum number of workers that should be using this module.
  int min_allowed_workers_;

  // The maximum number of workers allowed to access this module. Should be set
  // to greater than 1 iff the module is thread safe.
  int max_allowed_workers_;

  // Should workers be propagated. Set this to false for cases, e.g., `Queue`
  // where upstream and downstream modules are called by different workers.
  // Note, one should override the `AddActiveWorker` method in more complex
  // cases.
  bool propagate_workers_;
  DISALLOW_COPY_AND_ASSIGN(Module);
};

static inline void deadend(Context *ctx, bess::PacketBatch *batch) {
  ctx->silent_drops += batch->cnt();
  bess::Packet::Free(batch);
  batch->clear();
}

inline void Module::RunChooseModule(Context *ctx, gate_idx_t ogate_idx,
                                    bess::PacketBatch *batch) {
  bess::OGate *ogate;

  if (unlikely(batch->cnt() <= 0)) {
    return;
  }

  if (unlikely(ogate_idx >= ogates_.size())) {
    deadends_[ctx->wid] += batch->cnt();
    deadend(ctx, batch);
    return;
  }

  ogate = ogates_[ogate_idx];

  if (unlikely(!ogate)) {
    deadends_[ctx->wid] += batch->cnt();
    deadend(ctx, batch);
    return;
  }

  for (auto &hook : ogate->hooks()) {
    hook->ProcessBatch(batch);
  }

  ctx->task->AddToRun(ogate->igate(), batch);
}

inline void Module::RunNextModule(Context *ctx, bess::PacketBatch *batch) {
  RunChooseModule(ctx, 0, batch);
}

inline void Module::DropPacket(Context *ctx, bess::Packet *pkt) {
  ctx->task->dead_batch()->add(pkt);
  deadends_[ctx->wid]++;
  if (static_cast<size_t>(ctx->task->dead_batch()->cnt()) >=
      bess::PacketBatch::kMaxBurst) {
    deadend(ctx, ctx->task->dead_batch());
  }
}

inline void Module::EmitPacket(Context *ctx, bess::Packet *pkt,
                               gate_idx_t ogate_idx) {
  // Check if valid ogate is set
  if (unlikely(ogates_.size() <= ogate_idx) || unlikely(!ogates_[ogate_idx])) {
    DropPacket(ctx, pkt);
    return;
  }

  Task *task = ctx->task;

  // Put a packet into the ogate
  bess::OGate *ogate = ogates_[ogate_idx];
  bess::IGate *igate = ogate->igate();
  bess::PacketBatch *batch = task->get_gate_batch(ogate);
  if (!batch) {
    if (!ogate->hooks().empty()) {
      // Having separate batch to run ogate hooks
      batch = task->AllocPacketBatch();
      task->set_gate_batch(ogate, batch);
      ctx->gate_with_hook[ctx->gate_with_hook_cnt++] = ogate_idx;
    } else {
      // If no ogate hooks, just use next igate batch
      batch = task->get_gate_batch(igate);
      if (batch == nullptr) {
        batch = task->AllocPacketBatch();
        task->AddToRun(igate, batch);
        task->set_gate_batch(ogate, batch);
      } else {
        task->set_gate_batch(ogate, task->get_gate_batch(igate));
      }
      ctx->gate_without_hook[ctx->gate_without_hook_cnt++] = ogate_idx;
    }
  }

  if (static_cast<size_t>(batch->cnt()) >= bess::PacketBatch::kMaxBurst) {
    if (!ogate->hooks().empty()) {
      for (auto &hook : ogate->hooks()) {
        hook->ProcessBatch(task->get_gate_batch(ogate));
      }
      task->AddToRun(igate, task->get_gate_batch(ogate));
      batch = task->AllocPacketBatch();
      task->set_gate_batch(ogate, batch);
    } else {
      // allocate a new batch and push
      batch = task->AllocPacketBatch();
      task->set_gate_batch(ogate, batch);
      task->AddToRun(igate, batch);
    }
  }

  batch->add(pkt);
}

inline void Module::ProcessOGates(Context *ctx) {
  Task *task = ctx->task;

  // Running ogate hooks, then add next igate to be scheduled
  for (int i = 0; i < ctx->gate_with_hook_cnt; i++) {
    bess::OGate *ogate = ogates_[ctx->gate_with_hook[i]];  // should not be null
    for (auto &hook : ogate->hooks()) {
      hook->ProcessBatch(task->get_gate_batch(ogate));
    }
    task->AddToRun(ogate->igate(), task->get_gate_batch(ogate));
    task->set_gate_batch(ogate, nullptr);
  }

  // Clear packet batch for ogates without hook
  for (int i = 0; i < ctx->gate_without_hook_cnt; i++) {
    bess::OGate *ogate =
        ogates_[ctx->gate_without_hook[i]];  // should not be null
    task->set_gate_batch(ogate, nullptr);
  }

  ctx->gate_with_hook_cnt = 0;
  ctx->gate_without_hook_cnt = 0;
}

inline void Module::RunSplit(Context *ctx, const gate_idx_t *out_gates,
                             bess::PacketBatch *mixed_batch) {
  int pkt_cnt = mixed_batch->cnt();
  if (unlikely(pkt_cnt <= 0)) {
    return;
  }

  int gate_cnt = ogates_.size();
  if (unlikely(gate_cnt <= 0)) {
    deadends_[ctx->wid] += mixed_batch->cnt();
    deadend(ctx, mixed_batch);
    return;
  }

  for (int i = 0; i < pkt_cnt; i++) {
    EmitPacket(ctx, mixed_batch->pkts()[i], out_gates[i]);
  }

  mixed_batch->clear();
}

// run all per-thread initializers
void init_module_worker(void);

#if SN_TRACE_MODULES
void _trace_before_call(Module *mod, Module *next, bess::PacketBatch *batch);

void _trace_after_call(void);
#endif

template <typename T>
static inline int is_active_gate(const std::vector<T *> &gates,
                                 gate_idx_t idx) {
  return idx < gates.size() && gates.size() && gates[idx];
}

// Unsafe, but faster version. for offset use Attribute_offset().
template <typename T>
static inline T *_ptr_attr_with_offset(bess::metadata::mt_offset_t offset,
                                       const bess::Packet *pkt) {
  promise(offset >= 0);
  uintptr_t addr = pkt->metadata<uintptr_t>() + offset;
  return reinterpret_cast<T *>(addr);
}

template <typename T>
static inline T _get_attr_with_offset(bess::metadata::mt_offset_t offset,
                                      const bess::Packet *pkt) {
  return *_ptr_attr_with_offset<T>(offset, pkt);
}

template <typename T>
static inline void _set_attr_with_offset(bess::metadata::mt_offset_t offset,
                                         bess::Packet *pkt, T val) {
  *(_ptr_attr_with_offset<T>(offset, pkt)) = val;
}

// Safe version.
template <typename T>
static T *ptr_attr_with_offset(bess::metadata::mt_offset_t offset,
                               bess::Packet *pkt) {
  return bess::metadata::IsValidOffset(offset)
             ? _ptr_attr_with_offset<T>(offset, pkt)
             : nullptr;
}

template <typename T>
static T get_attr_with_offset(bess::metadata::mt_offset_t offset,
                              const bess::Packet *pkt) {
  return bess::metadata::IsValidOffset(offset)
             ? _get_attr_with_offset<T>(offset, pkt)
             : T();
}

template <typename T>
static inline void set_attr_with_offset(bess::metadata::mt_offset_t offset,
                                        bess::Packet *pkt, T val) {
  if (bess::metadata::IsValidOffset(offset)) {
    _set_attr_with_offset<T>(offset, pkt, val);
  }
}

// Slowest but easiest.
// TODO(melvin): These ought to be members of Module
template <typename T>
static inline T *ptr_attr(Module *m, int attr_id, bess::Packet *pkt) {
  return ptr_attr_with_offset<T>(m->attr_offset(attr_id), pkt);
}

template <typename T>
static inline T get_attr(Module *m, int attr_id, const bess::Packet *pkt) {
  return get_attr_with_offset<T>(m->attr_offset(attr_id), pkt);
}

template <typename T>
static inline void set_attr(Module *m, int attr_id, bess::Packet *pkt, T val) {
  set_attr_with_offset(m->attr_offset(attr_id), pkt, val);
}

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
