#include "round_robin.h"

const Commands RoundRobin::cmds = {
    {"set_mode", "RoundRobinCommandSetModeArg",
     MODULE_CMD_FUNC(&RoundRobin::CommandSetMode), 0},
    {"set_gates", "RoundRobinCommandSetGatesArg",
     MODULE_CMD_FUNC(&RoundRobin::CommandSetGates), 0},
};

CommandResponse RoundRobin::Init(const bess::pb::RoundRobinArg &arg) {
  CommandResponse err;

  if (arg.gates_size() > MAX_RR_GATES) {
    return CommandFailure(EINVAL, "no more than %d gates", MAX_RR_GATES);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    int elem = arg.gates(i);
    gates_[i] = elem;
    if (!is_valid_gate(gates_[i])) {
      return CommandFailure(EINVAL, "invalid gate %d", gates_[i]);
    }
  }
  ngates_ = arg.gates_size();

  if (arg.mode().length()) {
    if (arg.mode() == "packet") {
      per_packet_ = 1;
    } else if (arg.mode() == "batch") {
      per_packet_ = 0;
    } else {
      return CommandFailure(EINVAL,
                            "argument must be either 'packet' or 'batch'");
    }
  }

  return CommandSuccess();
}

CommandResponse RoundRobin::CommandSetMode(
    const bess::pb::RoundRobinCommandSetModeArg &arg) {
  if (arg.mode() == "packet") {
    per_packet_ = 1;
  } else if (arg.mode() == "batch") {
    per_packet_ = 0;
  } else {
    return CommandFailure(EINVAL,
                          "argument must be either 'packet' or 'batch'");
  }
  return CommandSuccess();
}

CommandResponse RoundRobin::CommandSetGates(
    const bess::pb::RoundRobinCommandSetGatesArg &arg) {
  if (arg.gates_size() > MAX_RR_GATES) {
    return CommandFailure(EINVAL, "no more than %d gates", MAX_RR_GATES);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    int elem = arg.gates(i);
    gates_[i] = elem;
    if (!is_valid_gate(gates_[i])) {
      return CommandFailure(EINVAL, "invalid gate %d", gates_[i]);
    }
  }

  ngates_ = arg.gates_size();
  return CommandSuccess();
}

void RoundRobin::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];

  if (per_packet_) {
    for (int i = 0; i < batch->cnt(); i++) {
      out_gates[i] = gates_[current_gate_];
      current_gate_ = (current_gate_ + 1) % ngates_;
    }
    RunSplit(out_gates, batch);
  } else {
    gate_idx_t gate = gates_[current_gate_];
    current_gate_ = (current_gate_ + 1) % ngates_;
    RunChooseModule(gate, batch);
  }
}

ADD_MODULE(RoundRobin, "rr", "splits packets evenly with round robin")
