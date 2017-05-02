#ifndef BESS_MODULES_IPLOOKUP_H_
#define BESS_MODULES_IPLOOKUP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class IPLookup final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  IPLookup() : Module(), lpm_(), default_gate_() {}

  CommandResponse Init(const bess::pb::IPLookupArg &arg);

  void DeInit() override;

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::IPLookupCommandAddArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  struct rte_lpm *lpm_;
  gate_idx_t default_gate_;
};

#endif  // BESS_MODULES_IPLOOKUP_H_
