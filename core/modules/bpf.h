#ifndef BESS_MODULES_BPF_H_
#define BESS_MODULES_BPF_H_

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

class BPF final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  CommandResponse Init(const bess::pb::BPFArg &arg);
  void DeInit() override;

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::BPFArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  struct filter filters_[MAX_FILTERS + 1] = {};
  int n_filters_ = {};

  inline void process_batch_1filter(bess::PacketBatch *batch);
};

#endif  // BESS_MODULES_BPF_H_
