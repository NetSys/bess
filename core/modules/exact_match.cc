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

#include "exact_match.h"

#include <string>
#include <vector>

#include "../utils/endian.h"
#include "../utils/format.h"

// XXX: this is repeated in many modules. get rid of them when converting .h to
// .hh, etc... it's in defined in some old header
static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

const Commands ExactMatch::cmds = {
    {"add", "ExactMatchCommandAddArg", MODULE_CMD_FUNC(&ExactMatch::CommandAdd),
     Command::THREAD_UNSAFE},
    {"delete", "ExactMatchCommandDeleteArg",
     MODULE_CMD_FUNC(&ExactMatch::CommandDelete), Command::THREAD_UNSAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&ExactMatch::CommandClear),
     Command::THREAD_UNSAFE},
    {"set_default_gate", "ExactMatchCommandSetDefaultGateArg",
     MODULE_CMD_FUNC(&ExactMatch::CommandSetDefaultGate),
     Command::THREAD_SAFE}};

CommandResponse ExactMatch::AddFieldOne(const bess::pb::Field &field,
                                        const bess::pb::FieldData &mask,
                                        int idx) {
  int size = field.num_bytes();
  uint64_t mask64 = 0;
  if (mask.encoding_case() == bess::pb::FieldData::kValueInt) {
    mask64 = mask.value_int();
  } else if (mask.encoding_case() == bess::pb::FieldData::kValueBin) {
    bess::utils::Copy(reinterpret_cast<uint8_t *>(&mask64),
                      mask.value_bin().c_str(), mask.value_bin().size());
  }

  Error ret;
  if (field.position_case() == bess::pb::Field::kAttrName) {
    ret = table_.AddField(this, field.attr_name(), size, mask64, idx);
    if (ret.first) {
      return CommandFailure(ret.first, "%s", ret.second.c_str());
    }
  } else if (field.position_case() == bess::pb::Field::kOffset) {
    ret = table_.AddField(field.offset(), size, mask64, idx);
    if (ret.first) {
      return CommandFailure(ret.first, "%s", ret.second.c_str());
    }
  } else {
    return CommandFailure(EINVAL,
                          "idx %d: must specify 'offset' or 'attr_name'", idx);
  }

  return CommandSuccess();
}

CommandResponse ExactMatch::Init(const bess::pb::ExactMatchArg &arg) {
  if (arg.fields_size() != arg.masks_size() && arg.masks_size() != 0) {
    return CommandFailure(EINVAL,
                          "must provide masks for all fields (or no masks for "
                          "default match on all bits on all fields)");
  }

  for (auto i = 0; i < arg.fields_size(); ++i) {
    CommandResponse err;

    if (arg.masks_size() == 0) {
      bess::pb::FieldData emptymask;
      err = AddFieldOne(arg.fields(i), emptymask, i);
    } else {
      err = AddFieldOne(arg.fields(i), arg.masks(i), i);
    }

    if (err.error().code() != 0) {
      return err;
    }
  }

  default_gate_ = DROP_GATE;

  return CommandSuccess();
}

void ExactMatch::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t default_gate;
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  ExactMatchKey keys[bess::PacketBatch::kMaxBurst] __ymm_aligned;

  int cnt = batch->cnt();

  default_gate = ACCESS_ONCE(default_gate_);

  const auto buffer_fn = [&](bess::Packet *pkt, const ExactMatchField &f) {
    int attr_id = f.attr_id;
    if (attr_id >= 0) {
      return ptr_attr<uint8_t>(this, attr_id, pkt);
    }
    return pkt->head_data<uint8_t *>() + f.offset;
  };
  table_.MakeKeys(batch, buffer_fn, keys);
  table_.Find(keys, out_gates, cnt, default_gate);

  RunSplit(out_gates, batch);
}

std::string ExactMatch::GetDesc() const {
  return bess::utils::Format("%zu fields, %zu rules", table_.num_fields(),
                             table_.Size());
}

void ExactMatch::RuleFieldsFromPb(
    const RepeatedPtrField<bess::pb::FieldData> &fields,
    bess::utils::ExactMatchRuleFields *rule) {
  for (auto i = 0; i < fields.size(); i++) {
    int field_size = table_.get_field(i).size;

    bess::pb::FieldData current = fields.Get(i);

    if (current.encoding_case() == bess::pb::FieldData::kValueBin) {
      const std::string &f_obj = fields.Get(i).value_bin();
      rule->push_back(std::vector<uint8_t>(f_obj.begin(), f_obj.end()));
    } else {
      rule->emplace_back();
      uint64_t rule64 = current.value_int();
      for (int j = 0; j < field_size; j++) {
        rule->back().push_back(rule64 & 0xFFULL);
        rule64 >>= 8;
      }
    }
  }
}

CommandResponse ExactMatch::CommandAdd(
    const bess::pb::ExactMatchCommandAddArg &arg) {
  gate_idx_t gate = arg.gate();
  CommandResponse err;

  if (!is_valid_gate(gate)) {
    return CommandFailure(EINVAL, "Invalid gate: %hu", gate);
  }

  if (arg.fields_size() == 0) {
    return CommandFailure(EINVAL, "'fields' must be a list");
  }

  ExactMatchRuleFields rule;
  RuleFieldsFromPb(arg.fields(), &rule);

  Error ret = table_.AddRule(gate, rule);
  if (ret.first) {
    return CommandFailure(ret.first, "%s", ret.second.c_str());
  }

  return CommandSuccess();
}

CommandResponse ExactMatch::CommandDelete(
    const bess::pb::ExactMatchCommandDeleteArg &arg) {
  CommandResponse err;

  if (arg.fields_size() == 0) {
    return CommandFailure(EINVAL, "argument must be a list");
  }

  ExactMatchRuleFields rule;
  RuleFieldsFromPb(arg.fields(), &rule);

  Error ret = table_.DeleteRule(rule);
  if (ret.first) {
    return CommandFailure(ret.first, "%s", ret.second.c_str());
  }

  return CommandSuccess();
}

CommandResponse ExactMatch::CommandClear(const bess::pb::EmptyArg &) {
  table_.ClearRules();
  return CommandSuccess();
}

CommandResponse ExactMatch::CommandSetDefaultGate(
    const bess::pb::ExactMatchCommandSetDefaultGateArg &arg) {
  default_gate_ = arg.gate();
  return CommandSuccess();
}

ADD_MODULE(ExactMatch, "em", "Multi-field classifier with an exact match table")
