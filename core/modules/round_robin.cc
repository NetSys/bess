#include "round_robin.h"

const Commands RoundRobin::cmds = {
    {"set_mode", "RoundRobinCommandSetModeArg",
     MODULE_CMD_FUNC(&RoundRobin::CommandSetMode), 0},
    {"set_gates", "RoundRobinCommandSetGatesArg",
     MODULE_CMD_FUNC(&RoundRobin::CommandSetGates), 0},
};

pb_error_t RoundRobin::Init(const bess::pb::RoundRobinArg &arg) {
  pb_error_t err;

  if (arg.gates_size() > MAX_RR_GATES) {
    return pb_error(EINVAL, "no more than %d gates", MAX_RR_GATES);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    int elem = arg.gates(i);
    gates_[i] = elem;
    if (!is_valid_gate(gates_[i])) {
      return pb_error(EINVAL, "invalid gate %d", gates_[i]);
    }
  }
  ngates_ = arg.gates_size();

  if (arg.mode().length()) {
    if (arg.mode() == "packet") {
      per_packet_ = 1;
    } else if (arg.mode() == "batch") {
      per_packet_ = 0;
    } else {
      return pb_error(EINVAL, "argument must be either 'packet' or 'batch'");
    }
  }

  return pb_errno(0);
}

pb_cmd_response_t RoundRobin::CommandSetMode(
    const bess::pb::RoundRobinCommandSetModeArg &arg) {
  pb_cmd_response_t response;

  if (arg.mode() == "packet") {
    per_packet_ = 1;
  } else if (arg.mode() == "batch") {
    per_packet_ = 0;
  } else {
    set_cmd_response_error(
        &response,
        pb_error(EINVAL, "argument must be either 'packet' or 'batch'"));
    return response;
  }
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t RoundRobin::CommandSetGates(
    const bess::pb::RoundRobinCommandSetGatesArg &arg) {
  pb_cmd_response_t response;

  if (arg.gates_size() > MAX_RR_GATES) {
    set_cmd_response_error(
        &response, pb_error(EINVAL, "no more than %d gates", MAX_RR_GATES));
    return response;
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    int elem = arg.gates(i);
    gates_[i] = elem;
    if (!is_valid_gate(gates_[i])) {
      set_cmd_response_error(&response,
                             pb_error(EINVAL, "invalid gate %d", gates_[i]));
      return response;
    }
  }

  ngates_ = arg.gates_size();
  set_cmd_response_error(&response, pb_errno(0));
  return response;
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
