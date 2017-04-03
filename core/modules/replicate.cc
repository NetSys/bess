#include "replicate.h"

const Commands Replicate::cmds = {
  {"set_gates", "ReplicateCommandSetGatesArg",
    MODULE_CMD_FUNC(&Replicate::CommandSetGates), 0},
};

pb_error_t Replicate::Init(const bess::pb::ReplicateArg &arg) {
  pb_error_t err;

  if (arg.gates_size() > MAX_REPL_GATES) {
    return pb_error(EINVAL, "no more than %d gates", MAX_REPL_GATES);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    int elem = arg.gates(i);
    gates_[i] = elem;
  }
  ngates_ = arg.gates_size();

  return pb_errno(0);
}

pb_cmd_response_t Replicate::CommandSetGates(
    const bess::pb::ReplicateCommandSetGatesArg &arg) {
  pb_cmd_response_t response;

  if (arg.gates_size() > MAX_REPL_GATES) {
    set_cmd_response_error(
        &response, pb_error(EINVAL, "no more than %d gates", MAX_REPL_GATES));
    return response;
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    int elem = arg.gates(i);
    gates_[i] = elem;
  }

  ngates_ = arg.gates_size();
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

void Replicate::ProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch out_gates[ngates_];
  memset(out_gates, 0, sizeof(bess::PacketBatch) * ngates_);

  for (int i = 0; i < batch->cnt(); i++) {
    out_gates[0].add(batch->pkts()[i]);
    for(int j = 1; j < ngates_; j++){
      out_gates[j].add(bess::Packet::copy(batch->pkts()[i]));
    }
  }

  for(int j = 0; j < ngates_; j++){
    RunChooseModule(gates_[j], &(out_gates[j]));
  }
}

ADD_MODULE(Replicate, "repl", "makes a copy of a packet and sends it out over n gates")
