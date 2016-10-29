#ifndef __BPF_H__
#define __BPF_H__

#include "../message.h"
#include "../module.h"

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
  virtual pb_error_t Init(const bess::protobuf::BPFArg &arg);
  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  pb_error_t CommandAdd(const bess::protobuf::BPFArg &arg);
  pb_error_t CommandClear(const bess::protobuf::BPFCommandClearArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;

 private:
  struct filter filters_[MAX_FILTERS + 1] = {{0}};
  int n_filters_ = {0};

  inline void process_batch_1filter(struct pkt_batch *batch);
};

#endif
