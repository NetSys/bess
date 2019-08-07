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
     MODULE_CMD_FUNC(&Replicate::CommandSetGates), Command::THREAD_SAFE},
};

CommandResponse Replicate::Init(const bess::pb::ReplicateArg &arg) {
  CommandResponse err;

  if (arg.gates_size() > kMaxGates) {
    return CommandFailure(EINVAL, "no more than %d gates", kMaxGates);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    int elem = arg.gates(i);
    gates_[i] = elem;
    gates_[i + kMaxGates] = elem; // Initialize the "backup" array
  }
  for (int i = arg.gates_size(); i <= kMaxGates; i++) {
    gates_[i] = DROP_GATE;
    gates_[i + kMaxGates] = DROP_GATE; // Initialize the "backup" array
  }
  ngates_[0] = arg.gates_size();
  ngates_[1] = arg.gates_size();
  active_gates = 0;

  return CommandSuccess();
}

// The mechanics for thread-safe change here are rather dumb. In theory
// we can get packet duplicates if the configuration change is so fast
// that the packet processing is still using the (now)backup table while
// it is being written to.
// It is possible to guard against that in here, but it will be rather
// expensive, easier to do that in the control plane by doing the following:
// 1. Do not reorder the gate vector. Stop packet processing if doing so.
// 2. Do not delete a gate when making a change without stopping packet 
// processing - replace it with DROP_GATE instead to keep the vector in same
// order
// 3. Add new gates at the back.

CommandResponse Replicate::CommandSetGates(
    const bess::pb::ReplicateCommandSetGatesArg &arg) {

  int backup_gates = (active_gates + 1) % 2;

  if (arg.gates_size() > kMaxGates) {
    return CommandFailure(EINVAL, "no more than %d gates", kMaxGates);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    gates_[i + backup_gates * kMaxGates] = arg.gates(i);
  }
  for (int i = arg.gates_size(); i <= kMaxGates; i++) {
    gates_[i + backup_gates * kMaxGates] = DROP_GATE;
  }

  //swap active and backup gates portions of the vector
  ngates_[backup_gates] = arg.gates_size();

  STORE_BARRIER();

  active_gates = backup_gates;
  backup_gates = (active_gates + 1) % 2;

  for (int i = 0; i < arg.gates_size(); i++) {
    gates_[i + backup_gates * kMaxGates] = arg.gates(i);
  }
  for (int i = arg.gates_size(); i <= kMaxGates; i++) {
    gates_[i + backup_gates * kMaxGates] = DROP_GATE;
  }

  ngates_[backup_gates] = arg.gates_size();

  return CommandSuccess();
}

void Replicate::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *tocopy = batch->pkts()[i];
    for (int j = 1; j < ngates_[active_gates]; j++) {
      bess::Packet *newpkt = bess::Packet::copy(tocopy);
      if (newpkt) {
        EmitPacket(ctx, newpkt, gates_[j + active_gates * kMaxGates]);
      }
    }
    EmitPacket(ctx, tocopy, gates_[active_gates * kMaxGates]);
  }
}

ADD_MODULE(Replicate, "repl",
           "makes a copy of a packet and sends it out over n gates")
