#ifndef BESS_TASK_H_
#define BESS_TASK_H_

#include <cstdint>

typedef uint16_t task_id_t;

class Module;

namespace bess {
  template <typename CallableTask>
  class LeafTrafficClass;
}  // namespace bess

struct task_result {
  uint64_t packets;
  uint64_t bits;
};

class Task {
 public:
  Task(Module *m, void *arg) : module_(m), arg_(arg) {};

  virtual ~Task() {};
  
  struct task_result operator()();

 private:
  void DestroyTC();

  Module *module_;
  void *arg_;
};

class ModuleTask : public Task {
 public:
  ModuleTask(Task t, bess::LeafTrafficClass<Task> *c)
      : Task(t), c_(c) {};

  ~ModuleTask() {};

  void SetTC(bess::LeafTrafficClass<Task> *c) {
    c_ = c;
  }

  bess::LeafTrafficClass<Task> *GetTC() {
    return c_;
  }

 private:
  bess::LeafTrafficClass<Task> *c_;
};

#endif  // BESS_TASK_H_
