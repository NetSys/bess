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

#ifndef BESS_MODULES_RANDOM_SPLIT_H_
#define BESS_MODULES_RANDOM_SPLIT_H_

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../utils/random.h"

// Maximum number of output gates to allow.
#define MAX_SPLIT_GATES 16384

// RandomSplit splits and drop packets.
class RandomSplit final : public Module {
 public:
  RandomSplit() : Module() { max_allowed_workers_ = Worker::kMaxWorkers; }

  static const gate_idx_t kNumOGates = MAX_GATES;
  static const Commands cmds;

  CommandResponse Init(const bess::pb::RandomSplitArg &arg);
  CommandResponse CommandSetDroprate(
      const bess::pb::RandomSplitCommandSetDroprateArg &arg);
  CommandResponse CommandSetGates(
      const bess::pb::RandomSplitCommandSetGatesArg &arg);

  void ProcessBatch(const Task *task, bess::PacketBatch *batch) override;

 private:
  Random rng_;  // Random number generator
  double drop_rate_;
  gate_idx_t gates_[MAX_SPLIT_GATES];
  gate_idx_t ngates_;
};

#endif  // BESS_MODULES_RANDOM_SPLIT_H_
