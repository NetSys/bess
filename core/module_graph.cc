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

#include "module_graph.h"

#include <glog/logging.h>

#include "gate_hooks/track.h"
#include "module.h"

std::map<std::string, Module *> ModuleGraph::all_modules_;
std::unordered_set<std::string> ModuleGraph::tasks_;
bool ModuleGraph::changes_made_ = false;

void ModuleGraph::UpdateParentsAs(
    Module *task, Module *module,
    std::unordered_set<Module *> &visited_modules) {
  visited_modules.insert(module);

  if (module->is_task()) {
    module->AddParentTask(task);
    return;
  } else {
    std::vector<bess::OGate *> ogates = module->ogates();
    for (size_t i = 0; i < ogates.size(); i++) {
      if (!ogates[i]) {
        break;
      }
      Module *child = ogates[i]->igate()->module();
      if (visited_modules.count(child) != 0) {
        continue;
      }
      UpdateParentsAs(task, child, visited_modules);
    }
  }
}

void ModuleGraph::UpdateSingleTaskGraph(Module *task_module) {
  std::unordered_set<Module *> visited_modules;
  visited_modules.insert(task_module);

  std::vector<bess::OGate *> ogates = task_module->ogates();
  for (size_t i = 0; i < ogates.size(); i++) {
    if (!ogates[i]) {
      break;
    }

    Module *child = ogates[i]->igate()->module();
    if (visited_modules.count(child) != 0) {
      continue;
    }

    UpdateParentsAs(task_module, child, visited_modules);
  }
}

void ModuleGraph::UpdateTaskGraph() {
  if (!changes_made_)
    return;

  CleanTaskGraph();

  for (auto const &task : tasks_) {
    auto it = all_modules_.find(task);
    if (it != all_modules_.end()) {
      UpdateSingleTaskGraph(it->second);
    }
  }

  changes_made_ = false;
}

void ModuleGraph::CleanTaskGraph() {
  for (auto const &task : tasks_) {
    auto it = all_modules_.find(task);
    if (it != all_modules_.end()) {
      it->second->ClearParentTasks();
    }
  }
}

const std::map<std::string, Module *> &ModuleGraph::GetAllModules() {
  return all_modules_;
}

bool ModuleGraph::HasModuleOfClass(const ModuleBuilder *builder) {
  for (auto const &e : all_modules_) {
    if (e.second->module_builder() == builder) {
      return true;
    }
  }
  return false;
}

// Creates a module to the graph.
Module *ModuleGraph::CreateModule(const ModuleBuilder &builder,
                                  const std::string &module_name,
                                  const google::protobuf::Any &arg,
                                  pb_error_t *perr) {
  Module *m =
      builder.CreateModule(module_name, &bess::metadata::default_pipeline);

  CommandResponse ret = m->InitWithGenericArg(arg);
  {
    google::protobuf::Any empty;

    if (ret.data().SerializeAsString() != empty.SerializeAsString()) {
      LOG(WARNING) << module_name << "::" << builder.class_name()
                   << " Init() returned non-empty response: "
                   << ret.data().DebugString();
    }
  }

  if (ret.error().code() != 0) {
    *perr = ret.error();
    delete m;
    return nullptr;
  }

  if (m->is_task()) {
    if (!tasks_.insert(m->name()).second) {
      *perr = pb_errno(ENOMEM);
      delete m;
      return nullptr;
    }
  }

  bool module_added = all_modules_.insert({m->name(), m}).second;
  if (!module_added) {
    *perr = pb_errno(ENOMEM);
    delete m;
    return nullptr;
  }

  return m;
}

void ModuleGraph::DestroyModule(Module *m, bool erase) {
  changes_made_ = true;

  m->Destroy();

  if (erase) {
    all_modules_.erase(m->name());
  }

  if (m->is_task()) {
    tasks_.erase(m->name());
  }

  delete m;
}

void ModuleGraph::DestroyAllModules() {
  changes_made_ = true;

  for (auto it = all_modules_.begin(); it != all_modules_.end();) {
    auto it_next = std::next(it);
    DestroyModule(it->second, false);
    all_modules_.erase(it);
    it = it_next;
  }
}

int ModuleGraph::ConnectModules(Module *module, gate_idx_t ogate_idx,
                                Module *m_next, gate_idx_t igate_idx,
                                bool skip_default_hooks) {
  if (ogate_idx >= module->module_builder()->NumOGates() ||
      ogate_idx >= MAX_GATES) {
    return -EINVAL;
  }

  if (igate_idx >= m_next->module_builder()->NumIGates() ||
      igate_idx >= MAX_GATES) {
    return -EINVAL;
  }

  changes_made_ = true;

  int ret = module->ConnectGate(ogate_idx, m_next, igate_idx);
  if (ret != 0)
    return ret;

  if (!skip_default_hooks) {
    // Gate tracking is enabled by default
    module->ogates()[ogate_idx]->AddHook(new Track());
  }

  return 0;
}

int ModuleGraph::DisconnectModule(Module *module, gate_idx_t ogate_idx) {
  if (ogate_idx >= module->module_builder()->NumOGates()) {
    return -EINVAL;
  }

  changes_made_ = true;

  module->DisconnectGate(ogate_idx);

  return 0;
}

std::string ModuleGraph::GenerateDefaultName(
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

void ModuleGraph::PropagateActiveWorker() {
  for (auto &pair : all_modules_) {
    Module *m = pair.second;
    m->ResetActiveWorkerSet();
  }
  for (int i = 0; i < Worker::kMaxWorkers; i++) {
    if (workers[i] == nullptr) {
      continue;
    }
    if (bess::TrafficClass *root = workers[i]->scheduler()->root()) {
      for (const auto &tc_pair : bess::TrafficClassBuilder::all_tcs()) {
        bess::TrafficClass *c = tc_pair.second;
        if (c->policy() == bess::POLICY_LEAF && c->Root() == root) {
          auto leaf = static_cast<bess::LeafTrafficClass *>(c);
          leaf->task()->AddActiveWorker(i);
        }
      }
    }
  }
}
