#ifndef BESS_MODULES_HASHLB_H_
#define BESS_MODULES_HASHLB_H_

#include "../module.h"
#include "../module_msg.pb.h"

#define MAX_HLB_GATES 16384

/* TODO: add symmetric mode (e.g., LB_L4_SYM), v6 mode, etc. */
enum LbMode {
  LB_L2, /* dst MAC + src MAC */
  LB_L3, /* src IP + dst IP */
  LB_L4  /* L4 proto + src IP + dst IP + src port + dst port */
};

class HashLB final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  HashLB() : Module(), gates_(), num_gates_(), mode_() {}

  CommandResponse Init(const bess::pb::HashLBArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandSetMode(const bess::pb::HashLBCommandSetModeArg &arg);
  CommandResponse CommandSetGates(
      const bess::pb::HashLBCommandSetGatesArg &arg);

 private:
  void LbL2(bess::PacketBatch *batch, gate_idx_t *ogates);
  void LbL3(bess::PacketBatch *batch, gate_idx_t *ogates);
  void LbL4(bess::PacketBatch *batch, gate_idx_t *ogates);

  gate_idx_t gates_[MAX_HLB_GATES];
  int num_gates_;
  enum LbMode mode_;
};

#endif  // BESS_MODULES_HASHLB_H_
