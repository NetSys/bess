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

#include "message.h"
#include "metadata.h"
#include "utils/common.h"

class Module;
class ModuleBuilder;

// Represents a node in `module_graph_`.
class Node {
 public:
  // Creates a new Node that represents `module_`.
  Node(Module *module) : module_(module), children_() {}

  // Add a child to the node.
  bool AddChild(const std::string &child) {
    return children_.insert(child).second;
  }

  // Remove a child from the node.
  void RemoveChild(const std::string &child) { children_.erase(child); }

  const Module *module() const { return module_; }
  const std::unordered_set<std::string> &children() const { return children_; }

 private:
  // Module that this Node represents.
  Module *module_;

  // Children of `module_` in the pipeline.
  std::unordered_set<std::string> children_;

  DISALLOW_COPY_AND_ASSIGN(Node);
};

// Manages a global graph of modules
class ModuleGraph {
 public:
  // Return true if any module from the builder exists
  static bool HasModuleOfClass(const ModuleBuilder *);

  // Creates a module to the graph.
  static Module *CreateModule(const ModuleBuilder &builder,
                              const std::string &module_name,
                              const google::protobuf::Any &arg,
                              pb_error_t *perr);

  // Removes a module to the graph. Returns 0 on success, -errno
  // otherwise.
  static int DestroyModule(Module *m, bool erase = true);
  static void DestroyAllModules();

  static const std::map<std::string, Module *> &GetAllModules();

  static std::string GenerateDefaultName(const std::string &class_name,
                                         const std::string &default_template);

  // Connects two modules (`to` and `from`) together in `module_graph_`.
  static bool AddEdge(const std::string &from, const std::string &to);

  // Disconnects two modules (`to` and `from`) together in `module_graph_`.
  static bool RemoveEdge(const std::string &from, const std::string &to);

 private:
  // Updates the parents of modules with tasks by traversing `module_graph_` and
  // ignoring all modules that are not tasks.
  static bool UpdateTaskGraph();

  // Finds the next module that implements a task along the pipeline.
  // If if find any, then the current task becomes the parent of the next task.
  static bool FindNextTask(const std::string &node_name,
                           const std::string &parent_name,
                           std::unordered_set<std::string> *visited);

  // A graph of all the modules in the current pipeline.
  static std::unordered_map<std::string, Node> module_graph_;

  // All modules that are tasks in the current pipeline.
  static std::unordered_set<std::string> tasks_;

  // All modules
  static std::map<std::string, Module *> all_modules_;
};

/*!
 * Update information about what workers are accessing what module.
 */
void propagate_active_worker();

#endif
