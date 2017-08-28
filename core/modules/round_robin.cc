// Copyright (c) 2014-2016, The Regents of the University of California.
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

#include "round_robin.h"

const Commands RoundRobin::cmds = {
    {"set_mode", "RoundRobinCommandSetModeArg",
     MODULE_CMD_FUNC(&RoundRobin::CommandSetMode), Command::THREAD_UNSAFE},
    {"set_gates", "RoundRobinCommandSetGatesArg",
     MODULE_CMD_FUNC(&RoundRobin::CommandSetGates), Command::THREAD_UNSAFE},
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

  if (ngates_ <= 0) {
    bess::Packet::Free(batch);
    return;
  }

  if (per_packet_) {
    for (int i = 0; i < batch->cnt(); i++) {
      out_gates[i] = gates_[current_gate_];
      if (++current_gate_ >= ngates_) {
        current_gate_ = 0;
      }
    }
    RunSplit(out_gates, batch);
  } else {
    gate_idx_t gate = gates_[current_gate_];
    if (++current_gate_ >= ngates_) {
      current_gate_ = 0;
    }
    RunChooseModule(gate, batch);
  }
}

ADD_MODULE(RoundRobin, "rr", "splits packets evenly with round robin")
