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

#ifndef BESS_MODULE_GRAPH_H_
#define BESS_MODULE_GRAPH_H_

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "gate.h"
#include "message.h"
#include "metadata.h"
#include "utils/common.h"

using bess::gate_idx_t;

class Module;
class ModuleBuilder;

// Manages a global graph of modules
class ModuleGraph {
 public:
  // Return true if any module from the builder exists
  static bool HasModuleOfClass(const ModuleBuilder *);

  // Creates a module.
  static Module *CreateModule(const ModuleBuilder &builder,
                              const std::string &module_name,
                              const google::protobuf::Any &arg,
                              pb_error_t *perr);

  // Removes a module. Returns 0 on success, -errno
  // otherwise.
  static void DestroyModule(Module *m, bool erase = true);
  static void DestroyAllModules();

  static int ConnectModules(Module *module, gate_idx_t ogate_idx,
                            Module *m_next, gate_idx_t igate_idx,
                            bool skip_default_hooks = false);
  static int DisconnectModule(Module *module, gate_idx_t ogate_idx);

  static const std::map<std::string, Module *> &GetAllModules();

  static std::string GenerateDefaultName(const std::string &class_name,
                                         const std::string &default_template);

  // Updates the parents of tasks
  static void UpdateTaskGraph();

  // Cleans the parents of modules
  static void CleanTaskGraph();

  // Update information about what workers are accessing what module
  static void PropagateActiveWorker();

 private:
  static void UpdateParentsAs(Module *parent_task, Module *module,
                              std::unordered_set<Module *> &visited_modules);
  static void UpdateSingleTaskGraph(Module *module);

  static void PropagateIGatePriority(
      bess::IGate *igate, std::unordered_set<bess::IGate *> &visited_igate,
      uint32_t priority);

  static void SetIGatePriority(Module *task_module);
  static void SetUniqueGateIdx();
  static void ConfigureTasks();

  // All modules that are tasks in the current pipeline.
  static std::unordered_set<std::string> tasks_;

  // All modules
  static std::map<std::string, Module *> all_modules_;

  static uint32_t gate_cnt_;
  // Check if any changes on module graphs
  static bool changes_made_;
};

#endif
