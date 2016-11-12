#ifndef BESS_TASK_H_
#define BESS_TASK_H_

#include "gate.h"
#include <cstdint>

#include "utils/cdlist.h"

typedef uint16_t task_id_t;

struct tc;
class Module;

struct task {
  struct tc *c;

  Module *m;
  void *arg;

  struct cdlist_item tc;
  struct cdlist_item all_tasks;
};

struct task_result {
  uint64_t packets;
  uint64_t bits;
};

struct task *task_create(Module *m, void *arg);

void task_destroy(struct task *t);

static inline int task_is_attached(struct task *t) {
  return (t->c != nullptr);
}

void task_attach(struct task *t, struct tc *c);
void task_detach(struct task *t);

// FIXME: make this inline, once breaking task -> module dependency
struct task_result task_scheduled(struct task *t);

void assign_default_tc(int wid, struct task *t);
void process_orphan_tasks();

task_id_t task_to_tid(struct task *t);

#endif  // BESS_TASK_H_
