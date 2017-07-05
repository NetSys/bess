// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "replicate.h"

const Commands Replicate::cmds = {
    {"set_gates", "ReplicateCommandSetGatesArg",
     MODULE_CMD_FUNC(&Replicate::CommandSetGates), Command::THREAD_UNSAFE},
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
