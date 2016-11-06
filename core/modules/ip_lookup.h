#ifndef __IP_LOOKUP_H__
#define __IP_LOOKUP_H__

#include "../module.h"

class IPLookup : public Module {
 public:
  IPLookup() : Module(), lpm_(), default_gate_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::pb::EmptyArg &arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  bess::pb::ModuleCommandResponse CommandAddPb(
      const bess::pb::IPLookupCommandAddArg &arg);
  bess::pb::ModuleCommandResponse CommandClearPb(const bess::pb::EmptyArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  struct rte_lpm *lpm_;
  gate_idx_t default_gate_;
};

#endif
