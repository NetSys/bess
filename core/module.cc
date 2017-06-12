#include "module.h"

#include <glog/logging.h>

#include <algorithm>
#include <sstream>

#include "gate.h"
#include "hooks/tcpdump.h"
#include "hooks/track.h"
#include "mem_alloc.h"
#include "scheduler.h"
#include "utils/pcap.h"
#include "worker.h"

const Commands Module::cmds;

std::map<std::string, Module *> ModuleBuilder::all_modules_;
std::unordered_map<std::string, Node> ModuleBuilder::module_graph_;
std::unordered_set<std::string> ModuleBuilder::tasks_;

bool ModuleBuilder::AddEdge(const std::string &from, const std::string &to) {
  auto from_it = module_graph_.find(from);
  if (from_it == module_graph_.end() || module_graph_.count(to) == 0) {
    return false;
  }
  from_it->second.AddChild(to);
  return UpdateTaskGraph();
}

bool ModuleBuilder::RemoveEdge(const std::string &from, const std::string &to) {
  auto from_node = module_graph_.find(from);
  if (from_node == module_graph_.end() || module_graph_.count(to) == 0) {
    return false;
  }

  from_node->second.RemoveChild(to);

  // We need to regenerate the task graph.
  for (auto const &task : tasks_) {
    auto it = all_modules_.find(task);
    if (it != all_modules_.end()) {
      it->second->parent_tasks_.clear();
    }
  }
  return UpdateTaskGraph();
}

bool ModuleBuilder::UpdateTaskGraph() {
  for (auto const &task : tasks_) {
    std::unordered_set<std::string> visited;
    if (!FindNextTask(task, task, &visited)) {
      return false;
    }
  }
  return true;
}

bool ModuleBuilder::FindNextTask(const std::string &node_name,
                                 const std::string &parent_name,
                                 std::unordered_set<std::string> *visited) {
  visited->insert(node_name);
  // While traversing the module graph, if `node` is in the task graph and is
  // not `parent`, then it must be  the child of `parent`.
  if (node_name != parent_name && tasks_.find(node_name) != tasks_.end()) {
    auto parent_it = all_modules_.find(parent_name);
    auto node_it = all_modules_.find(node_name);
    if (parent_it == all_modules_.end() || node_it == all_modules_.end()) {
      return false;
    }

    Module *node = node_it->second;
    Module *parent = parent_it->second;
    for (Module *it : node->parent_tasks_) {
      if (it == parent) {
        return true;
      }
    }
    node->parent_tasks_.push_back(parent);
    return true;
  }

  auto node_it = module_graph_.find(node_name);
  if (node_it == module_graph_.end()) {
    return false;
  }

  for (auto &child_name : node_it->second.children()) {
    if (visited->count(child_name) > 0) {
      continue;
    }
    if (!FindNextTask(child_name, parent_name, visited)) {
      return false;
    }
  }
  return true;
}

Module *ModuleBuilder::CreateModule(const std::string &name,
                                    bess::metadata::Pipeline *pipeline) const {
  Module *m = module_generator_();
  m->set_name(name);
  m->set_module_builder(this);
  m->set_pipeline(pipeline);
  return m;
}

bool ModuleBuilder::AddModule(Module *m) {
  if (m->is_task_) {
    if (!tasks_.insert(m->name()).second) {
      return false;
    }
  }

  bool module_added = all_modules_.insert({m->name(), m}).second;
  if (!module_added) {
    return false;
  }

  return module_graph_
      .emplace(std::piecewise_construct, std::forward_as_tuple(m->name()),
               std::forward_as_tuple(m))
      .second;
}

int ModuleBuilder::DestroyModule(Module *m, bool erase) {
  int ret;
  m->DeInit();

  // disconnect from upstream modules.
  for (size_t i = 0; i < m->igates_.size(); i++) {
    ret = m->DisconnectModulesUpstream(i);
    if (ret) {
      return ret;
    }
  }

  // disconnect downstream modules
  for (size_t i = 0; i < m->ogates_.size(); i++) {
    ret = m->DisconnectModules(i);
    if (ret) {
      return ret;
    }
  }

  m->DestroyAllTasks();
  m->DeregisterAllAttributes();

  if (erase) {
    all_modules_.erase(m->name());
  }

  module_graph_.erase(m->name());
  if (m->is_task_) {
    tasks_.erase(m->name());
  }

  delete m;
  return 0;
}

void ModuleBuilder::DestroyAllModules() {
  int ret;
  for (auto it = all_modules_.begin(); it != all_modules_.end();) {
    auto it_next = std::next(it);
    ret = DestroyModule(it->second, false);
    if (ret) {
      LOG(ERROR) << "Error destroying module '" << it->first
                 << "' (errno = " << ret << ")";
    } else {
      all_modules_.erase(it);
    }
    it = it_next;
  }
}

bool ModuleBuilder::RegisterModuleClass(
    std::function<Module *()> module_generator, const std::string &class_name,
    const std::string &name_template, const std::string &help_text,
    const gate_idx_t igates, const gate_idx_t ogates, const Commands &cmds,
    module_init_func_t init_func) {
  all_module_builders_holder().emplace(
      std::piecewise_construct, std::forward_as_tuple(class_name),
      std::forward_as_tuple(module_generator, class_name, name_template,
                            help_text, igates, ogates, cmds, init_func));
  return true;
}

bool ModuleBuilder::DeregisterModuleClass(const std::string &class_name) {
  // Check if the module builder exists
  auto it = all_module_builders_holder().find(class_name);
  if (it == all_module_builders_holder().end()) {
    return false;
  }

  // Check if any module of the class still exists
  const ModuleBuilder *builder = &(it->second);
  for (auto const &e : all_modules()) {
    if (e.second->module_builder() == builder) {
      return false;
    }
  }

  all_module_builders_holder().erase(it);
  return true;
}

std::string ModuleBuilder::GenerateDefaultName(
    const std::string &class_name, const std::string &default_template) {
  std::string name_template;

  if (default_template == "") {
    std::ostringstream ss;
    char last_char = '\0';
    for (auto t : class_name) {
      if (last_char != '\0' && islower(last_char) && isupper(t))
        ss << '_';

      ss << char(tolower(t));
      last_char = t;
    }
    name_template = ss.str();
  } else {
    name_template = default_template;
  }

  for (int i = 0;; i++) {
    std::ostringstream ss;
    ss << name_template << i;
    std::string name = ss.str();

    if (!all_modules_.count(name))
      return name;
  }

  promise_unreachable();
}

std::map<std::string, ModuleBuilder> &ModuleBuilder::all_module_builders_holder(
    bool reset) {
  // Maps from class names to port builders.  Tracks all port classes (via their
  // PortBuilders).
  static std::map<std::string, ModuleBuilder> all_module_builders;

  if (reset) {
    all_module_builders.clear();
  }

  return all_module_builders;
}

const std::map<std::string, ModuleBuilder>
    &ModuleBuilder::all_module_builders() {
  return all_module_builders_holder();
}

const std::map<std::string, Module *> &ModuleBuilder::all_modules() {
  return all_modules_;
}

CommandResponse ModuleBuilder::RunCommand(
    Module *m, const std::string &user_cmd,
    const google::protobuf::Any &arg) const {
  for (auto &cmd : cmds_) {
    if (user_cmd == cmd.cmd) {
      bool workers_running = false;
      for (const auto wid : m->active_workers_) {
        workers_running |= is_worker_running(wid);
      }
      if (!cmd.mt_safe && workers_running) {
        return CommandFailure(EBUSY,
                              "There is a running worker and command "
                              "'%s' is not MT safe",
                              cmd.cmd.c_str());
      }

      return cmd.func(m, arg);
    }
  }

  return CommandFailure(ENOTSUP, "'%s' does not support command '%s'",
                        class_name_.c_str(), user_cmd.c_str());
}

CommandResponse ModuleBuilder::RunInit(Module *m,
                                       const google::protobuf::Any &arg) const {
  return init_func_(m, arg);
}

// -------------------------------------------------------------------------
CommandResponse Module::InitWithGenericArg(const google::protobuf::Any &arg) {
  return module_builder_->RunInit(this, arg);
}

CommandResponse Module::Init(const bess::pb::EmptyArg &) {
  return CommandSuccess();
}

void Module::DeInit() {}

struct task_result Module::RunTask(void *) {
  CHECK(0);  // You must override this function
  return task_result();
}

void Module::ProcessBatch(bess::PacketBatch *) {
  CHECK(0);  // You must override this function
}

task_id_t Module::RegisterTask(void *arg) {
  ModuleTask *t = new ModuleTask(arg, nullptr);

  std::string leafname = std::string("!leaf_") + name_ + std::string(":") +
                         std::to_string(tasks_.size());
  bess::LeafTrafficClass<Task> *c =
      bess::TrafficClassBuilder::CreateTrafficClass<
          bess::LeafTrafficClass<Task>>(leafname, Task(this, arg, t));

  add_tc_to_orphan(c, -1);

  tasks_.push_back(t);
  return tasks_.size() - 1;
}

void Module::DestroyAllTasks() {
  for (auto task : tasks_) {
    auto c = task->GetTC();
    CHECK(detach_tc(c));
    delete c;
    delete task;
  }
  tasks_.clear();
}

void Module::DeregisterAllAttributes() {
  for (const auto &it : attrs_) {
    pipeline_->DeregisterAttribute(it.name);
  }
}

placement_constraint Module::ComputePlacementConstraints(
    std::unordered_set<const Module *> *visited) const {
  // Take the constraints we have.
  int constraint = node_constraints_;
  // Follow the rest of the tree unless we have already been here.
  if (visited->find(this) == visited->end()) {
    visited->insert(this);
    for (size_t i = 0; i < ogates_.size(); i++) {
      if (ogates_[i]) {
        auto next = static_cast<Module *>(ogates_[i]->arg());
        // Restrict constraints to account for other modules in the pipeline.
        constraint &= next->ComputePlacementConstraints(visited);
        if (constraint == 0) {
          LOG(WARNING) << "At " << name_
                       << " after accounting for constraints from module "
                       << next->name_ << " no feasible placement exists.";
        }
      }
    }
  }
  return constraint;
}

void Module::AddActiveWorker(int wid, const ModuleTask *t) {
  if (!HaveVisitedWorker(t)) {  // Have not already accounted for
                                // worker.
    active_workers_[wid] = true;
    visited_tasks_.push_back(t);
    // Check if we should propagate downstream. We propagate if either
    // `propagate_workers_` is true or if the current module created the task.
    bool propagate = propagate_workers_;
    if (!propagate) {
      for (const auto task : tasks_) {
        if (t == task) {
          propagate = true;
          break;
        }
      }
    }
    if (propagate) {
      for (auto ogate : ogates_) {
        if (ogate) {
          auto next = static_cast<Module *>(ogate->arg());
          next->AddActiveWorker(wid, t);
        }
      }
    }
  }
}

CheckConstraintResult Module::CheckModuleConstraints() const {
  int active_workers = num_active_workers();
  CheckConstraintResult valid = CHECK_OK;
  if (active_workers < min_allowed_workers_ ||
      active_workers > max_allowed_workers_) {
    LOG(ERROR) << "Mismatch in number of workers for module " << name_
               << " min required " << min_allowed_workers_ << " max allowed "
               << max_allowed_workers_ << " attached workers "
               << active_workers;
    if (active_workers > max_allowed_workers_) {
      LOG(ERROR) << "Violates thread safety, returning fatal error";
      return CHECK_FATAL_ERROR;
    }
  }

  for (int wid = 0; wid < Worker::kMaxWorkers; wid++) {
    if (active_workers_[wid]) {
      placement_constraint socket = 1ull << workers[wid]->socket();
      if ((socket & node_constraints_) == 0) {
        LOG(ERROR) << "Worker wid " << wid
                   << " does not meet placement constraints for module "
                   << name_;
        valid = CHECK_NONFATAL_ERROR;
      }
    }
  }
  return valid;
}

int Module::AddMetadataAttr(const std::string &name, size_t size,
                            bess::metadata::Attribute::AccessMode mode) {
  int ret;

  if (attrs_.size() >= bess::metadata::kMaxAttrsPerModule)
    return -ENOSPC;

  if (name.empty())
    return -EINVAL;

  if (size < 1 || size > bess::metadata::kMetadataAttrMaxSize)
    return -EINVAL;

  // We do not allow a module to have multiple attributes with the same name
  for (const auto &it : attrs_) {
    if (it.name == name) {
      return -EEXIST;
    }
  }

  if ((ret = pipeline_->RegisterAttribute(name, size))) {
    return ret;
  }

  bess::metadata::Attribute attr;
  attr.name = name;
  attr.size = size;
  attr.mode = mode;
  attr.scope_id = -1;

  attrs_.push_back(attr);

  return attrs_.size() - 1;
}

/* returns -errno if fails */
int Module::ConnectModules(gate_idx_t ogate_idx, Module *m_next,
                           gate_idx_t igate_idx) {
  bess::OGate *ogate;
  bess::IGate *igate;

  if (ogate_idx >= module_builder_->NumOGates() || ogate_idx >= MAX_GATES) {
    return -EINVAL;
  }

  if (igate_idx >= m_next->module_builder()->NumIGates() ||
      igate_idx >= MAX_GATES) {
    return -EINVAL;
  }

  /* already being used? */
  if (is_active_gate<bess::OGate>(ogates_, ogate_idx)) {
    return -EBUSY;
  }

  if (ogate_idx >= ogates_.size()) {
    ogates_.resize(ogate_idx + 1, nullptr);
  }

  ogate = new bess::OGate(this, ogate_idx, m_next);
  if (!ogate) {
    return -ENOMEM;
  }
  ogates_[ogate_idx] = ogate;

  if (igate_idx >= m_next->igates_.size()) {
    m_next->igates_.resize(igate_idx + 1, nullptr);
  }

  if (m_next->igates_[igate_idx] == nullptr) {
    igate = new bess::IGate(m_next, igate_idx, m_next);
    m_next->igates_[igate_idx] = igate;
  } else {
    igate = m_next->igates_[igate_idx];
  }

  ogate->set_igate(igate);
  ogate->set_igate_idx(igate_idx);

  // Gate tracking is enabled by default
  ogate->AddHook(new Track());
  igate->PushOgate(ogate);

  // Update graph
  return !ModuleBuilder::AddEdge(name_, m_next->name_);
}

int Module::DisconnectModules(gate_idx_t ogate_idx) {
  bess::OGate *ogate;
  bess::IGate *igate;

  if (ogate_idx >= module_builder_->NumOGates()) {
    return -EINVAL;
  }

  /* no error even if the ogate is unconnected already */
  if (!is_active_gate<bess::OGate>(ogates_, ogate_idx)) {
    return 0;
  }

  ogate = ogates_[ogate_idx];
  if (!ogate) {
    return 0;
  }

  igate = ogate->igate();

  // Remove edge in module graph.
  if (!ModuleBuilder::RemoveEdge(name_, igate->module()->name_)) {
    return 1;
  }

  /* Does the igate become inactive as well? */
  igate->RemoveOgate(ogate);
  if (igate->ogates_upstream().empty()) {
    Module *m_next = igate->module();
    m_next->igates_[igate->gate_idx()] = nullptr;
    igate->ClearHooks();
    delete igate;
  }

  ogates_[ogate_idx] = nullptr;
  ogate->ClearHooks();
  delete ogate;

  return 0;
}

int Module::DisconnectModulesUpstream(gate_idx_t igate_idx) {
  bess::IGate *igate;

  if (igate_idx >= module_builder_->NumIGates()) {
    return -EINVAL;
  }

  /* no error even if the igate is unconnected already */
  if (!is_active_gate<bess::IGate>(igates_, igate_idx)) {
    return 0;
  }

  igate = igates_[igate_idx];
  if (!igate) {
    return 0;
  }

  for (const auto &ogate : igate->ogates_upstream()) {
    Module *m_prev = ogate->module();
    m_prev->ogates_[ogate->gate_idx()] = nullptr;
    ogate->ClearHooks();

    // Remove edge in module graph
    if (!ModuleBuilder::RemoveEdge(ogate->module()->name_, name_)) {
      return 1;
    }

    delete ogate;
  }

  igates_[igate_idx] = nullptr;
  igate->ClearHooks();
  delete igate;

  return 0;
}

void Module::RunSplit(const gate_idx_t *out_gates,
                      bess::PacketBatch *mixed_batch) {
  int cnt = mixed_batch->cnt();
  int num_pending = 0;

  bess::Packet **p_pkt = &mixed_batch->pkts()[0];

  gate_idx_t pending[bess::PacketBatch::kMaxBurst];
  bess::PacketBatch batches[bess::PacketBatch::kMaxBurst];

  bess::PacketBatch **splits = ctx.splits();

  // phase 1: collect unique ogates into pending[] and add packets to local
  // batches, using splits to remember the association between an ogate and a
  // local batch
  for (int i = 0; i < cnt; i++) {
    bess::PacketBatch *batch;
    gate_idx_t ogate;

    ogate = out_gates[i];
    batch = splits[ogate];
    if (!batch) {
      batch = splits[ogate] = &batches[num_pending];
      batch->clear();
      pending[num_pending] = ogate;
      num_pending++;
    }

    batch->add(*(p_pkt++));
  }

  // phase 2: clear splits, since it may be reentrant.
  for (int i = 0; i < num_pending; i++) {
    splits[pending[i]] = nullptr;
  }

  // phase 3: fire
  for (int i = 0; i < num_pending; i++)
    RunChooseModule(pending[i], &batches[i]);
}

#if SN_TRACE_MODULES
#define MAX_TRACE_DEPTH 32
#define MAX_TRACE_BUFSIZE 4096

struct callstack {
  int depth;

  int newlined;
  int indent[MAX_TRACE_DEPTH];
  int curr_indent;

  int buflen;
  char buf[MAX_TRACE_BUFSIZE];
};

__thread struct callstack worker_callstack;

void _trace_start(Module *mod, char *type) {
  struct callstack *s = &worker_callstack;

  DCHECK_EQ(s->depth, 0);
  DCHECK_EQ(s->buflen, 0);

  s->buflen = snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
                       "Worker %d %-8s | %s", current_wid, type, mod->name());

  s->curr_indent = s->buflen;
}

void _trace_end(int print_out) {
  struct callstack *s = &worker_callstack;

  DCHECK_EQ(s->depth, 0);
  s->buflen = 0;
  s->newlined = 0;

  if (print_out) {
    DLOG(INFO) << s->buf;
  }
}

void _trace_before_call(Module *mod, Module *next, bess::PacketBatch *batch) {
  struct callstack *s = &worker_callstack;
  int len;

  s->indent[s->depth] = s->curr_indent;

  if (s->newlined) {
    s->buflen += snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
                          "%*s", s->curr_indent, "");
  }

  len = snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
                 " ---(%d)--> %s", batch->cnt, next->name);

  s->buflen += len;
  s->curr_indent += len;

  s->depth++;
  DCHECK_LT(s->depth, MAX_TRACE_DEPTH);

  s->newlined = 0;
}

void _trace_after_call(void) {
  struct callstack *s = &worker_callstack;

  s->depth--;

  if (!s->newlined) {
    s->newlined = 1;

    s->buflen +=
        snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen, "\n");
  }

  s->curr_indent = s->indent[s->depth];
}
#endif

void propagate_active_worker() {
  for (auto &pair : ModuleBuilder::all_modules()) {
    Module *m = pair.second;
    m->ResetActiveWorkerSet();
  }
  for (int i = 0; i < Worker::kMaxWorkers; i++) {
    if (workers[i] == nullptr) {
      continue;
    }
    int socket = 1ull << workers[i]->socket();
    int core = workers[i]->core();
    bess::TrafficClass *root = workers[i]->scheduler()->root();
    if (root) {
      root->Traverse([i, socket, core](bess::TCChildArgs *args) {
        bess::TrafficClass *c = args->child();
        if (c->policy() == bess::POLICY_LEAF) {
          auto leaf = static_cast<bess::LeafTrafficClass<Task> *>(c);
          leaf->Task().AddActiveWorker(i);
        }
      });
    }
  }
}
