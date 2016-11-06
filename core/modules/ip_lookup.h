#ifndef BESS_MODULES_IPLOOKUP_H_
#define BESS_MODULES_IPLOOKUP_H_

#include "../module.h"

class IPLookup : public Module {
 public:
  IPLookup() : Module(), lpm_(), default_gate_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const google::protobuf::Any &arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  bess::pb::ModuleCommandResponse CommandAdd(const google::protobuf::Any &arg);
  bess::pb::ModuleCommandResponse CommandClear(
      const google::protobuf::Any &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  struct rte_lpm *lpm_;
  gate_idx_t default_gate_;
};

#endif  // BESS_MODULES_IPLOOKUP_H_
