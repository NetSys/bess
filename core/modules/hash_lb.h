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

#ifndef BESS_MODULES_HASHLB_H_
#define BESS_MODULES_HASHLB_H_

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../utils/exact_match_table.h"

using bess::utils::ExactMatchField;
using bess::utils::ExactMatchKey;
using bess::utils::ExactMatchKeyHash;
using bess::utils::ExactMatchTable;

class HashLB final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  HashLB()
      : Module(), gates_(), num_gates_(), mode_(), fields_table_(), hasher_(0) {
    max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const bess::pb::HashLBArg &arg);

  std::string GetDesc() const override;

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  CommandResponse CommandSetMode(const bess::pb::HashLBCommandSetModeArg &arg);
  CommandResponse CommandSetGates(
      const bess::pb::HashLBCommandSetGatesArg &arg);

 private:
  enum class Mode { kL2, kL3, kL4, kOther };
  static constexpr Mode kDefaultMode = Mode::kL4;

  template <Mode mode>
  inline void DoProcessBatch(Context *ctx, bess::PacketBatch *batch);

  static constexpr size_t kMaxGates = 16384;

  gate_idx_t gates_[kMaxGates];
  size_t num_gates_;
  Mode mode_;

  // No rules are ever added to this table, we just use it for MakeKeys().
  ExactMatchTable<int> fields_table_;
  ExactMatchKeyHash hasher_;
};

#endif  // BESS_MODULES_HASHLB_H_
