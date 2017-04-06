#ifndef BESS_MODULES_REPLICATE_H_
#define BESS_MODULES_REPLICATE_H_

#include "../module.h"
#include "../module_msg.pb.h"

class Replicate final : public Module {
 public:
  static const gate_idx_t kMaxGates = 32;
  static const gate_idx_t kNumOGates = kMaxGates;

  static const Commands cmds;

  Replicate() : Module(), gates_(), ngates_() {}

  pb_error_t Init(const bess::pb::ReplicateArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  /*!
   * Sets the number of output gates.
   */
  pb_cmd_response_t CommandSetGates(
      const bess::pb::ReplicateCommandSetGatesArg &arg);

 private:
  // ID number for each egress gate.
  gate_idx_t gates_[kMaxGates];
  // The total number of output gates
  int ngates_;
};

#endif  // BESS_MODULES_RELICATE_H_
