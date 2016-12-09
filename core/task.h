#ifndef BESS_TASK_H_
#define BESS_TASK_H_

#include <cstdint>

#include "gate.h"

typedef uint16_t task_id_t;

class Module;

namespace bess {
class LeafTrafficClass;
}  // namespace bess

struct task_result {
  uint64_t packets;
  uint64_t bits;
};

class Task {
 public:
  Task(Module *m, void *arg, bess::LeafTrafficClass *c);

  virtual ~Task();

  struct task_result Scheduled();

  void Attach(bess::LeafTrafficClass *c);

  inline const Module *m() const { return m_; }

  inline const bess::LeafTrafficClass *c() const { return c_; }

 private:
  Module *m_;
  void *arg_;
  bess::LeafTrafficClass *c_;
};

#endif  // BESS_TASK_H_
