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

class L2Forward final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  L2Forward() : Module(), l2_table_(), default_gate_() {}

  pb_error_t Init(const bess::pb::L2ForwardArg &arg);

  void DeInit() override;

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandAdd(const bess::pb::L2ForwardCommandAddArg &arg);
  pb_cmd_response_t CommandDelete(
      const bess::pb::L2ForwardCommandDeleteArg &arg);
  pb_cmd_response_t CommandSetDefaultGate(
      const bess::pb::L2ForwardCommandSetDefaultGateArg &arg);
  pb_cmd_response_t CommandLookup(
      const bess::pb::L2ForwardCommandLookupArg &arg);
  pb_cmd_response_t CommandPopulate(
      const bess::pb::L2ForwardCommandPopulateArg &arg);

 private:
  struct l2_table l2_table_;
  gate_idx_t default_gate_;
};

#endif  // BESS_MODULES_L2FORWARD_H_
