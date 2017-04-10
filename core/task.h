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

class ModuleTask {
 public:
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
  void *arg_;
  bess::LeafTrafficClass<Task> *c_;
};

class Task {
 public:
  Task(Module *m, void *arg, ModuleTask *t) : module_(m), arg_(arg), t_(t) {};

  void Detach() {
    if (t_) {
      t_->SetTC(nullptr);
    }
  }

  void Attach(bess::LeafTrafficClass<Task> *c) {
    if (t_) {
      t_->SetTC(c);
    }
  }

  struct task_result operator()(void);

 private:
  Module *module_;
  void *arg_;
  ModuleTask *t_;
};

#endif  // BESS_TASK_H_
