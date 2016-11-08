#ifndef BESS_MODULES_IPLOOKUP_H_
#define BESS_MODULES_IPLOOKUP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class IPLookup : public Module {
 public:
  IPLookup() : Module(), lpm_(), default_gate_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::EmptyArg &arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  pb_cmd_response_t CommandAddPb(const bess::pb::IPLookupCommandAddArg &arg);
  pb_cmd_response_t CommandClearPb(const bess::pb::EmptyArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  struct rte_lpm *lpm_;
  gate_idx_t default_gate_;
};

#endif  // BESS_MODULES_IPLOOKUP_H_
