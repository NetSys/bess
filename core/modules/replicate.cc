#include "replicate.h"

const Commands Replicate::cmds = {
    {"set_gates", "ReplicateCommandSetGatesArg",
     MODULE_CMD_FUNC(&Replicate::CommandSetGates), 0},
};

CommandResponse Replicate::Init(const bess::pb::ReplicateArg &arg) {
  CommandResponse err;

  if (arg.gates_size() > kMaxGates) {
    return CommandFailure(EINVAL, "no more than %d gates", kMaxGates);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    int elem = arg.gates(i);
    gates_[i] = elem;
  }
  ngates_ = arg.gates_size();

  return CommandSuccess();
}

CommandResponse Replicate::CommandSetGates(
    const bess::pb::ReplicateCommandSetGatesArg &arg) {
  if (arg.gates_size() > kMaxGates) {
    return CommandFailure(EINVAL, "no more than %d gates", kMaxGates);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    gates_[i] = arg.gates(i);
  }

  ngates_ = arg.gates_size();
  return CommandSuccess();
}

void Replicate::ProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch out_gates[ngates_];
  for (int i = 0; i < ngates_; i++) {
    out_gates[i].clear();
  }

  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *tocopy = batch->pkts()[i];
    out_gates[0].add(tocopy);
    for (int j = 1; j < ngates_; j++) {
      bess::Packet *newpkt = bess::Packet::copy(tocopy);
      if (newpkt) {
        out_gates[j].add(newpkt);
      }
    }
  }

  for (int j = 0; j < ngates_; j++) {
    RunChooseModule(gates_[j], &(out_gates[j]));
  }
}

ADD_MODULE(Replicate, "repl",
           "makes a copy of a packet and sends it out over n gates")
