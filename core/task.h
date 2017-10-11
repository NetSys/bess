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

#ifndef BESS_TASK_H_
#define BESS_TASK_H_

#include <string>

struct task_result {
  bool block;
  uint32_t packets;
  uint64_t bits;
};

typedef uint16_t task_id_t;
typedef uint64_t placement_constraint;

class Module;
class ModuleTask;

namespace bess {
template <typename CallableTask>
class LeafTrafficClass;
}  // namespace bess

// Functor used by a leaf in a Worker's Scheduler to run a task in a module.
class Task {
 public:
  // When this task is scheduled it will execute 'm' with 'arg'.  When the
  // associated leaf is created/destroyed, 'module_task' will be updated.
  Task(Module *m, void *arg, ModuleTask *module_task)
      : module_(m), arg_(arg), module_task_(module_task) {}

  // Called when the leaf that owns this task is destroyed.
  void Detach();

  // Called when the leaf that owns this task is created.
  void Attach(bess::LeafTrafficClass<Task> *c);

  struct task_result operator()(void) const;

  /*!
   * Compute constraints for the pipeline starting at this task.
   */
  placement_constraint GetSocketConstraints() const;

  /*!
   * Add a worker to the set of workers that call this task.
   */
  void AddActiveWorker(int wid) const;

  Module *module() const { return module_; }

  ModuleTask *module_task() const { return module_task_; }

 private:
  // Used by operator().
  Module *module_;
  void *arg_;

  // Used to notify a module that a leaf is being created/destroyed.
  ModuleTask *module_task_;
};

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

#endif  // BESS_TASK_H_
