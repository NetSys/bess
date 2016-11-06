#ifndef __BPF_H__
#define __BPF_H__

#include "../message.h"
#include "../module.h"
#include "../module_msg.pb.h"

#define MAX_FILTERS 128

typedef u_int (*bpf_filter_func_t)(u_char *, u_int, u_int);

struct filter {
  bpf_filter_func_t func;
  int gate;

  size_t mmap_size; /* needed for munmap() */
  int priority;     /* higher number == higher priority */
  char *exp;        /* original filter expression string */
};

class BPF : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::pb::BPFArg &arg);
  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  bess::pb::ModuleCommandResponse CommandAddPb(const bess::pb::BPFArg &arg);
  bess::pb::ModuleCommandResponse CommandClearPb(const bess::pb::EmptyArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  struct filter filters_[MAX_FILTERS + 1] = {};
  int n_filters_ = {};

  inline void process_batch_1filter(struct pkt_batch *batch);
};

#endif
