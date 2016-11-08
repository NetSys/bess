#ifndef BESS_MODULES_L2FORWARD_H_
#define BESS_MODULES_L2FORWARD_H_

#include "../module.h"
#include "../module_msg.pb.h"

struct l2_entry {
  union {
    struct {
      uint64_t addr : 48;
      uint64_t gate : 15;
      uint64_t occupied : 1;
    };
    uint64_t entry;
  };
};

struct l2_table {
  struct l2_entry *table;
  uint64_t size;
  uint64_t size_power;
  uint64_t bucket;
  uint64_t count;
};

class L2Forward : public Module {
 public:
  L2Forward() : Module(), l2_table_(), default_gate_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::L2ForwardArg &arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandDelete(struct snobj *arg);
  struct snobj *CommandSetDefaultGate(struct snobj *arg);
  struct snobj *CommandLookup(struct snobj *arg);
  struct snobj *CommandPopulate(struct snobj *arg);

  bess::pb::ModuleCommandResponse CommandAddPb(
      const bess::pb::L2ForwardCommandAddArg &arg);
  bess::pb::ModuleCommandResponse CommandDeletePb(
      const bess::pb::L2ForwardCommandDeleteArg &arg);
  bess::pb::ModuleCommandResponse CommandSetDefaultGatePb(
      const bess::pb::L2ForwardCommandSetDefaultGateArg &arg);
  bess::pb::ModuleCommandResponse CommandLookupPb(
      const bess::pb::L2ForwardCommandLookupArg &arg);
  bess::pb::ModuleCommandResponse CommandPopulatePb(
      const bess::pb::L2ForwardCommandPopulateArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  struct l2_table l2_table_ = {};
  gate_idx_t default_gate_ = {};
};

#endif  // BESS_MODULES_L2FORWARD_H_
