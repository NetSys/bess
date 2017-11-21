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

#include "hash_lb.h"

#include <utility>
#include <vector>

/* Returns a value in [0, range) as a function of an opaque number.
 * Also see utils/random.h */
static inline uint16_t hash_range(uint32_t hashval, uint16_t range) {
#if 1
  union {
    uint64_t i;
    double d;
  } tmp;

  /* the resulting number is 1.(b0)(b1)..(b31)00000..00 */
  tmp.i = 0x3ff0000000000000ull | (static_cast<uint64_t>(hashval) << 20);

  return (tmp.d - 1.0) * range;
#else
  /* This IDIV instruction is significantly slower */
  return hashval % range;
#endif
}

static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

const Commands HashLB::cmds = {
    {"set_mode", "HashLBCommandSetModeArg",
     MODULE_CMD_FUNC(&HashLB::CommandSetMode), Command::THREAD_UNSAFE},
    {"set_gates", "HashLBCommandSetGatesArg",
     MODULE_CMD_FUNC(&HashLB::CommandSetGates), Command::THREAD_UNSAFE}};

CommandResponse HashLB::CommandSetMode(
    const bess::pb::HashLBCommandSetModeArg &arg) {
  std::vector<std::pair<int, int>> fields;
  if (arg.fields_size()) {
    for (const auto &f : arg.fields()) {
      fields.emplace_back(f.offset(), f.num_bytes());
    }
  } else if (arg.mode() == "l2") {
    fields.emplace_back(0, 6);  // dst mac
    fields.emplace_back(6, 6);  // src mac
  } else if (arg.mode() == "l3") {
    fields.emplace_back(26, 8);  // src + dst ip
  } else if (arg.mode() == "l4") {
    fields.emplace_back(26, 8);  // src + dst ip
    fields.emplace_back(23, 1);  // ip proto
    fields.emplace_back(34, 4);  // l4 ports
  } else {
    return CommandFailure(EINVAL, "available LB modes: l2, l3, l4");
  }

  fields_table_ = ExactMatchTable<int>();
  for (size_t i = 0; i < fields.size(); i++) {
    const auto &f = fields[i];
    const auto err = fields_table_.AddField(f.first, f.second, 0, i);
    if (err.first) {
      return CommandFailure(-err.first, "Error adding field %zu: %s", i,
                            err.second.c_str());
    }
  }
  hasher_ = ExactMatchKeyHash(fields_table_.total_key_size());

  return CommandSuccess();
}

CommandResponse HashLB::CommandSetGates(
    const bess::pb::HashLBCommandSetGatesArg &arg) {
  if (static_cast<size_t>(arg.gates_size()) > kMaxGates) {
    return CommandFailure(EINVAL, "HashLB can have at most %zu ogates",
                          kMaxGates);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    gates_[i] = arg.gates(i);
    if (!is_valid_gate(gates_[i])) {
      return CommandFailure(EINVAL, "Invalid ogate %d", gates_[i]);
    }
  }

  num_gates_ = arg.gates_size();
  return CommandSuccess();
}

CommandResponse HashLB::Init(const bess::pb::HashLBArg &arg) {
  bess::pb::HashLBCommandSetGatesArg gates_arg;
  *gates_arg.mutable_gates() = arg.gates();
  CommandResponse ret = CommandSetGates(gates_arg);
  if (ret.has_error()) {
    return ret;
  }

  bess::pb::HashLBCommandSetModeArg mode_arg;
  mode_arg.set_mode(arg.mode());
  *mode_arg.mutable_fields() = arg.fields();
  return CommandSetMode(mode_arg);
}

std::string HashLB::GetDesc() const {
  return bess::utils::Format("%zu fields", fields_table_.num_fields());
}

void HashLB::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  void *bufs[bess::PacketBatch::kMaxBurst];
  ExactMatchKey keys[bess::PacketBatch::kMaxBurst];

  size_t cnt = batch->cnt();
  for (size_t i = 0; i < cnt; i++) {
    bufs[i] = batch->pkts()[i]->head_data<void *>();
  }

  fields_table_.MakeKeys((const void **)bufs, keys, cnt);

  for (size_t i = 0; i < cnt; i++) {
    out_gates[i] = gates_[hash_range(hasher_(keys[i]), num_gates_)];
  }

  RunSplit(out_gates, batch);
}

ADD_MODULE(HashLB, "hash_lb",
           "splits packets on a flow basis with L2/L3/L4 header fields")
