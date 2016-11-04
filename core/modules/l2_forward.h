#ifndef __L2_FORWARD_H__
#define __L2_FORWARD_H__

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
  virtual pb_error_t Init(const google::protobuf::Any &arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandDelete(struct snobj *arg);
  struct snobj *CommandSetDefaultGate(struct snobj *arg);
  struct snobj *CommandLookup(struct snobj *arg);
  struct snobj *CommandPopulate(struct snobj *arg);

  bess::protobuf::ModuleCommandResponse CommandAdd(
      const google::protobuf::Any &arg);
  bess::protobuf::ModuleCommandResponse CommandDelete(
      const google::protobuf::Any &arg);
  bess::protobuf::ModuleCommandResponse CommandSetDefaultGate(
      const google::protobuf::Any &arg);
  bess::protobuf::ModuleCommandResponse CommandLookup(
      const google::protobuf::Any &arg);
  bess::protobuf::ModuleCommandResponse CommandPopulate(
      const google::protobuf::Any &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  struct l2_table l2_table_ = {0};
  gate_idx_t default_gate_ = {0};
};

#endif
