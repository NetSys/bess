// Copyright (c) 2017, The Regents of the University of California.
// Copyright (c) 2017, Vivian Fang.
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

#include "random_split.h"
#include <string>
#include <time.h>

static inline bool is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

const Commands RandomSplit::cmds = {
    {"set_droprate", "RandomSplitCommandSetDroprateArg",
     MODULE_CMD_FUNC(&RandomSplit::CommandSetDroprate), Command::THREAD_UNSAFE},
    {"set_gates", "RandomSplitCommandSetGatesArg",
     MODULE_CMD_FUNC(&RandomSplit::CommandSetGates), Command::THREAD_UNSAFE}};

CommandResponse RandomSplit::Init(const bess::pb::RandomSplitArg &arg) {
  double drop_rate = arg.drop_rate();
  if (drop_rate < 0 || drop_rate > 1) {
    return CommandFailure(EINVAL, "drop rate needs to be between [0, 1]");
  }
  drop_rate_ = drop_rate;

  if (arg.gates_size() > MAX_SPLIT_GATES) {
    return CommandFailure(EINVAL, "no more than %d gates", MAX_SPLIT_GATES);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    if (!is_valid_gate(arg.gates(i))) {
      return CommandFailure(EINVAL, "Invalid gate %d", gates_[i]);
    }
  }

  ngates_ = arg.gates_size();
  for (int i = 0; i < ngates_; i++) {
    gates_[i] = arg.gates(i);
  }

  return CommandSuccess();
}

CommandResponse RandomSplit::CommandSetDroprate(
    const bess::pb::RandomSplitCommandSetDroprateArg &arg) {
  double drop_rate = arg.drop_rate();
  if (drop_rate < 0 || drop_rate > 1) {
    return CommandFailure(EINVAL, "drop rate needs to be between [0, 1]");
  }
  drop_rate_ = drop_rate;

  return CommandSuccess();
}

CommandResponse RandomSplit::CommandSetGates(
    const bess::pb::RandomSplitCommandSetGatesArg &arg) {
  if (arg.gates_size() > MAX_SPLIT_GATES) {
    return CommandFailure(EINVAL, "no more than %d gates", MAX_SPLIT_GATES);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    if (!is_valid_gate(arg.gates(i))) {
      return CommandFailure(EINVAL, "Invalid gate %d", gates_[i]);
    }
  }

  ngates_ = arg.gates_size();
  for (int i = 0; i < ngates_; i++) {
    gates_[i] = arg.gates(i);
  }

  return CommandSuccess();
}

void RandomSplit::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  if (ngates_ <= 0) {
    bess::Packet::Free(batch);
    return;
  }

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    if (rng_.GetReal() > drop_rate_) {
      EmitPacket(ctx, pkt, gates_[rng_.GetRange(ngates_)]);
    } else {
      DropPacket(ctx, pkt);
    }
  }
}

ADD_MODULE(RandomSplit, "random_split", "randomly splits/drops packets")
