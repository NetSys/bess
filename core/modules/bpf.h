#ifndef BESS_MODULES_BPF_H_
#define BESS_MODULES_BPF_H_

#include <pcap.h>

#include <vector>

#include "../module.h"
#include "../module_msg.pb.h"

using bpf_filter_func_t = u_int (*)(u_char *, u_int, u_int);

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
  struct Filter {
#ifdef __x86_64
    bpf_filter_func_t func;
    size_t mmap_size;  // needed for munmap()
#else
    bpf_program il_code;
#endif
    int gate;
    int priority;     // higher number == higher priority
    std::string exp;  // original filter expression string
  };

  static bool Match(const Filter &, u_char *, u_int, u_int);

  void ProcessBatch1Filter(bess::PacketBatch *batch);

  std::vector<Filter> filters_;
};

#endif  // BESS_MODULES_BPF_H_
