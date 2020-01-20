// Copyright (c) 2014-2017, The Regents of the University of California.
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

#include "module.h"

#include <glog/logging.h>

#include <algorithm>
#include <sstream>

#include "gate.h"
#include "module_graph.h"
#include "scheduler.h"
#include "task.h"
#include "utils/pcap.h"

const Commands Module::cmds;

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
  if (ModuleGraph::HasModuleOfClass(builder))
    return false;

  all_module_builders_holder().erase(it);
  return true;
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

CommandResponse ModuleBuilder::RunCommand(
    Module *m, const std::string &user_cmd,
    const google::protobuf::Any &arg) const {
  for (auto &cmd : cmds_) {
    if (user_cmd == cmd.cmd) {
      if (cmd.mt_safe != Command::THREAD_SAFE && m->HasRunningWorker()) {
        return CommandFailure(EBUSY,
                              "There is a running worker and command "
                              "'%s' is not MT safe",
                              cmd.cmd.c_str());
      }

      return cmd.func(m, arg);
    }
  }

  if (user_cmd == "get_initial_arg") {
    CommandResponse ret;
    *ret.mutable_data() = m->initial_arg_;
    return ret;
  }

  return CommandFailure(ENOTSUP, "'%s' does not support command '%s'",
                        class_name_.c_str(), user_cmd.c_str());
}

CommandResponse ModuleBuilder::RunInit(Module *m,
                                       const google::protobuf::Any &arg) const {
  CommandResponse ret = init_func_(m, arg);
  if (!ret.has_error()) {
    m->initial_arg_ = arg;
  }
  return ret;
}

Module *ModuleBuilder::CreateModule(const std::string &name,
                                    bess::metadata::Pipeline *pipeline) const {
  Module *m = module_generator_();
  m->set_name(name);
  m->set_module_builder(this);
  m->set_pipeline(pipeline);
  return m;
}

// -------------------------------------------------------------------------
CommandResponse Module::InitWithGenericArg(const google::protobuf::Any &arg) {
  return module_builder_->RunInit(this, arg);
}

CommandResponse Module::Init(const bess::pb::EmptyArg &) {
  return CommandSuccess();
}

void Module::DeInit() {}

struct task_result Module::RunTask(Context *, bess::PacketBatch *, void *) {
  CHECK(0);  // You must override this function
  return task_result();
}

void Module::ProcessBatch(Context *, bess::PacketBatch *) {
  CHECK(0);  // You must override this function
}

task_id_t Module::RegisterTask(void *arg) {
  std::string leafname = std::string("!leaf_") + name_ + std::string(":") +
                         std::to_string(tasks_.size());
  Task *t = new Task(this, arg);
  bess::LeafTrafficClass *c =
      bess::TrafficClassBuilder::CreateTrafficClass<bess::LeafTrafficClass>(
          leafname, t);

  add_tc_to_orphan(c, -1);
  tasks_.push_back(t);
  return tasks_.size() - 1;
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
        auto next = static_cast<Module *>(ogates_[i]->next());
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

void Module::AddActiveWorker(int wid, const Task *t) {
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
          auto next = static_cast<Module *>(ogate->next());
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

int Module::ConnectGate(gate_idx_t ogate_idx, Module *m_next,
                        gate_idx_t igate_idx) {
  if (is_active_gate<bess::OGate>(ogates_, ogate_idx)) {
    return -EBUSY;
  }

  if (ogate_idx >= ogates_.size()) {
    ogates_.resize(ogate_idx + 1, nullptr);
  }

  if (igate_idx >= m_next->igates_.size()) {
    m_next->igates_.resize(igate_idx + 1, nullptr);
  }

  bess::OGate *ogate = new bess::OGate(this, ogate_idx, m_next);
  if (!ogate) {
    return -ENOMEM;
  }

  bess::IGate *igate;
  if (m_next->igates_[igate_idx] == nullptr) {
    igate = new bess::IGate(m_next, igate_idx);
    if (igate == nullptr) {
      return -ENOMEM;
    }
    m_next->igates_[igate_idx] = igate;
  } else {
    igate = m_next->igates_[igate_idx];
  }

  ogates_[ogate_idx] = ogate;

  ogate->SetIgate(igate);  // an ogate allowed to be connected to a single igate
  igate->PushOgate(ogate);  // an igate can connected to multiple ogates

  return 0;
}

int Module::DisconnectGate(gate_idx_t ogate_idx) {
  if (!is_active_gate<bess::OGate>(ogates_, ogate_idx)) {
    return 0;
  }

  bess::OGate *ogate = ogates_[ogate_idx];
  if (ogate == nullptr) {
    return 0;
  }

  bess::IGate *igate = ogate->igate();

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

void Module::Destroy() {
  // Per-module de-initialization
  DeInit();

  // disconnect from upstream modules.
  for (size_t i = 0; i < igates_.size(); i++) {
    DisconnectModulesUpstream(i);
  }

  // disconnect downstream modules
  for (size_t i = 0; i < ogates_.size(); i++) {
    int ret = DisconnectGate(i);
    CHECK_EQ(ret, 0);
  }

  DestroyAllTasks();
  DeregisterAllAttributes();
}

void Module::DisconnectModulesUpstream(gate_idx_t igate_idx) {
  bess::IGate *igate;

  CHECK_LT(igate_idx, module_builder_->NumIGates());

  /* no error even if the igate is unconnected already */
  if (!is_active_gate<bess::IGate>(igates_, igate_idx)) {
    return;
  }

  igate = igates_[igate_idx];
  if (igate == nullptr) {
    return;
  }

  for (const auto &ogate : igate->ogates_upstream()) {
    Module *m_prev = ogate->module();
    m_prev->ogates_[ogate->gate_idx()] = nullptr;
    ogate->ClearHooks();

    delete ogate;
  }

  igates_[igate_idx] = nullptr;
  igate->ClearHooks();
  delete igate;

  return;
}
void Module::DestroyAllTasks() {
  for (auto task : tasks_) {
    auto c = task->GetTC();

    int wid = c->WorkerId();
    if (wid >= 0) {
      bess::Scheduler *s = workers[wid]->scheduler();
      s->wakeup_queue().Remove(c);
    }

    CHECK(detach_tc(c));
    delete c;
  }
  tasks_.clear();
}

void Module::DeregisterAllAttributes() {
  for (const auto &it : attrs_) {
    pipeline_->DeregisterAttribute(it.name);
  }
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
