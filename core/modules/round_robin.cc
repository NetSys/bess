#include "round_robin.h"

const Commands<Module> RoundRobin::cmds = {
    {"set_mode", MODULE_FUNC &RoundRobin::CommandSetMode, 0},
    {"set_gates", MODULE_FUNC &RoundRobin::CommandSetGates, 0},
};

const PbCommands RoundRobin::pb_cmds = {
    {"set_mode", MODULE_CMD_FUNC(&RoundRobin::CommandSetModePb), 0},
    {"set_gates", MODULE_CMD_FUNC(&RoundRobin::CommandSetGatesPb), 0},
};

pb_error_t RoundRobin::InitPb(const bess::pb::RoundRobinArg &arg) {
  pb_error_t err;
  bess::pb::ModuleCommandResponse response;

  response = CommandSetGatesPb(arg.gate_arg());
  err = response.error();
  if (err.err() != 0) {
    return err;
  }

  response = CommandSetModePb(arg.mode_arg());
  err = response.error();
  if (err.err() != 0) {
    return err;
  }

  return pb_errno(0);
}

bess::pb::ModuleCommandResponse RoundRobin::CommandSetModePb(
    const bess::pb::RoundRobinCommandSetModeArg &arg) {
  bess::pb::ModuleCommandResponse response;

  switch (arg.mode()) {
    case bess::pb::RoundRobinCommandSetModeArg::PACKET:
      per_packet_ = 1;
      break;
    case bess::pb::RoundRobinCommandSetModeArg::BATCH:
      per_packet_ = 0;
      break;
    default:
      set_cmd_response_error(
          &response,
          pb_error(EINVAL, "argument must be either 'packet' or 'batch'"));
      return response;
  }
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

bess::pb::ModuleCommandResponse RoundRobin::CommandSetGatesPb(
    const bess::pb::RoundRobinCommandSetGatesArg &arg) {
  bess::pb::ModuleCommandResponse response;

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

struct snobj *RoundRobin::Init(struct snobj *arg) {
  struct snobj *t;

  if (!arg || snobj_type(arg) != TYPE_MAP) {
    return snobj_err(EINVAL, "empty argument");
  }

  if ((t = snobj_eval(arg, "gates"))) {
    struct snobj *err = CommandSetGates(t);
    if (err) {
      return err;
    }
  } else {
    return snobj_err(EINVAL, "'gates' must be specified");
  }

  if ((t = snobj_eval(arg, "mode"))) {
    return CommandSetMode(t);
  }

  return nullptr;
}

struct snobj *RoundRobin::CommandSetMode(struct snobj *arg) {
  const char *mode = snobj_str_get(arg);

  if (!mode) {
    return snobj_err(EINVAL, "argument must be a string");
  }

  if (strcmp(mode, "packet") == 0) {
    per_packet_ = 1;
  } else if (strcmp(mode, "batch") == 0) {
    per_packet_ = 0;
  } else {
    return snobj_err(EINVAL, "argument must be either 'packet' or 'batch'");
  }

  return nullptr;
}

struct snobj *RoundRobin::CommandSetGates(struct snobj *arg) {
  if (snobj_type(arg) == TYPE_INT) {
    int gates = snobj_int_get(arg);

    if (gates < 0 || gates > MAX_RR_GATES || gates > MAX_GATES) {
      return snobj_err(EINVAL, "no more than %d gates",
                       std::min(MAX_RR_GATES, MAX_GATES));
    }

    ngates_ = gates;
    for (int i = 0; i < gates; i++) {
      gates_[i] = i;
    }

  } else if (snobj_type(arg) == TYPE_LIST) {
    struct snobj *gates = arg;

    if (gates->size > MAX_RR_GATES) {
      return snobj_err(EINVAL, "no more than %d gates", MAX_RR_GATES);
    }

    for (size_t i = 0; i < gates->size; i++) {
      struct snobj *elem = snobj_list_get(gates, i);

      if (snobj_type(elem) != TYPE_INT) {
        return snobj_err(EINVAL, "'gate' must be an integer");
      }

      gates_[i] = snobj_int_get(elem);
      if (!is_valid_gate(gates_[i])) {
        return snobj_err(EINVAL, "invalid gate %d", gates_[i]);
      }
    }

    ngates_ = gates->size;

  } else {
    return snobj_err(EINVAL,
                     "argument must specify a gate "
                     "or a list of gates");
  }

  return nullptr;
}

void RoundRobin::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t out_gates[MAX_PKT_BURST];

  if (per_packet_) {
    for (int i = 0; i < batch->cnt; i++) {
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
