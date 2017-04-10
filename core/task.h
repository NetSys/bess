#ifndef BESS_TASK_H_
#define BESS_TASK_H_

#include <cstdint>

typedef uint16_t task_id_t;

class Module;
class Task;

namespace bess {
  template <typename CallableTask>
  class LeafTrafficClass;
}  // namespace bess

struct task_result {
  uint64_t packets;
  uint64_t bits;
};

// Stores the arguments of a task created by a module.
class ModuleTask {
 public:
  // Doesn't take ownership of 'arg' and 'c'.  'c' can be null and it
  // can be changed later with SetTC()
  ModuleTask(void *arg, bess::LeafTrafficClass<Task> *c)
      : arg_(arg), c_(c) {};

  ~ModuleTask() {};

  void *arg() {
    return arg_;
  }

  void SetTC(bess::LeafTrafficClass<Task> *c) {
    c_ = c;
  }

  bess::LeafTrafficClass<Task> *GetTC() {
    return c_;
  }

 private:
  void *arg_; // Auxiliary value passed to Module::RunTask().
  bess::LeafTrafficClass<Task> *c_; // Leaf TC associated with this task.
};

// Functor used by a leaf in a Worker's Scheduler to run a task in a module.
class Task {
 public:
  // When this task is scheduled it will execute 'm' with 'arg'.
  // When the associated leaf is created/destroyed, 't' will be updated.
  Task(Module *m, void *arg, ModuleTask *t) : module_(m), arg_(arg), t_(t) {};

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

  struct task_result operator()(void);

 private:
  // Used by operator().
  Module *module_;
  void *arg_;

  // Used to notify a module that a leaf is being created/destroyed.
  ModuleTask *t_;
};

#endif  // BESS_TASK_H_
