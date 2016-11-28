#ifndef BESS_MODULES_IPLOOKUP_H_
#define BESS_MODULES_IPLOOKUP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class IPLookup final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const PbCommands pb_cmds;

  IPLookup() : Module(), lpm_(), default_gate_() {}

  pb_error_t InitPb(const bess::pb::EmptyArg &arg);

  virtual void Deinit();

  virtual void ProcessBatch(bess::PacketBatch *batch);

  pb_cmd_response_t CommandAddPb(const bess::pb::IPLookupCommandAddArg &arg);
  pb_cmd_response_t CommandClearPb(const bess::pb::EmptyArg &arg);

 private:
  struct rte_lpm *lpm_;
  gate_idx_t default_gate_;
};

#endif  // BESS_MODULES_IPLOOKUP_H_
