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
    {"get_initial_arg", "EmptyArg", MODULE_CMD_FUNC(&ExactMatch::GetInitialArg),
     Command::THREAD_SAFE},
    {"get_runtime_config", "EmptyArg",
     MODULE_CMD_FUNC(&ExactMatch::GetRuntimeConfig), Command::THREAD_SAFE},
    {"set_runtime_config", "ExactMatchConfig",
     MODULE_CMD_FUNC(&ExactMatch::SetRuntimeConfig), Command::THREAD_UNSAFE},
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
  empty_masks_ = arg.masks_size() == 0;
  if (arg.fields_size() != arg.masks_size() && !empty_masks_) {
    return CommandFailure(EINVAL,
                          "must provide masks for all fields (or no masks for "
                          "default match on all bits on all fields)");
  }

  for (auto i = 0; i < arg.fields_size(); ++i) {
    CommandResponse err;

    if (empty_masks_) {
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

// Retrieves an ExactMatchArg that would reconstruct this module.
CommandResponse ExactMatch::GetInitialArg(const bess::pb::EmptyArg &) {
  bess::pb::ExactMatchArg r;

  for (size_t i = 0; i < table_.num_fields(); i++) {
    const ExactMatchField &f = table_.get_field(i);
    bess::pb::Field *ret_field = r.add_fields();
    if (f.attr_id >= 0) {
      ret_field->set_attr_name(all_attrs().at(f.attr_id).name);
    } else {
      ret_field->set_offset(f.offset);
    }
    ret_field->set_num_bytes(f.size);
    if (!empty_masks_) {
      bess::pb::FieldData *ret_mask = r.add_masks();
      // The optimal type for the mask (value_bin vs value_int) depends
      // on the wire encoding.  For the moment, we'll just use value_bin
      // with the field size, though; it's much simpler.  Or, perhaps
      // we should save the form used during configuration, and use
      // the same form here.
      const char *ptr = reinterpret_cast<const char *>(&f.mask);
      ret_mask->set_value_bin(ptr, f.size);
    }
  }
  return CommandSuccess(r);
}

// Retrieves an ExactMatchConfig that would restore this module's
// runtime configuration.
CommandResponse ExactMatch::GetRuntimeConfig(const bess::pb::EmptyArg &) {
  bess::pb::ExactMatchConfig r;
  using rule_t = bess::pb::ExactMatchCommandAddArg;

  r.set_default_gate(default_gate_);
  for (auto const &kv : table_) {
    auto const &key = kv.first;
    auto const &value = kv.second;
    rule_t *rule = r.add_rules();

    rule->set_gate(value);
    for (size_t i = 0; i < table_.num_fields(); i++) {
      const ExactMatchField &f = table_.get_field(i);
      bess::pb::FieldData *field = rule->add_fields();

      // See GetInitialArg above for why we only set_value_bin here.
      const char *ptr = reinterpret_cast<const char *>(&key.u64_arr[0]);
      field->set_value_bin(ptr + f.pos, f.size);
    }
  }
  std::sort(r.mutable_rules()->begin(), r.mutable_rules()->end(),
            [this](const rule_t &a, const rule_t &b) {
              // Primary sort key is gate number.
              if (a.gate() != b.gate()) {
                return a.gate() < b.gate();
              }
              // After that, sort by value-to-be-matched, in field order.
              for (size_t i = 0; i < table_.num_fields(); i++) {
                if (a.fields(i).value_bin() != b.fields(i).value_bin()) {
                  return a.fields(i).value_bin() < b.fields(i).value_bin();
                }
              }
              // Huh: a, b exactly equal - should not happen unless
              // std::sort() is poor.
              return false;
            });
  return CommandSuccess(r);
}

Error ExactMatch::AddRule(const bess::pb::ExactMatchCommandAddArg &arg) {
  gate_idx_t gate = arg.gate();

  if (!is_valid_gate(gate)) {
    return std::make_pair(EINVAL,
                          bess::utils::Format("Invalid gate: %hu", gate));
  }

  if (arg.fields_size() == 0) {
    return std::make_pair(EINVAL, "'fields' must be a list");
  }

  ExactMatchRuleFields rule;
  RuleFieldsFromPb(arg.fields(), &rule);

  return table_.AddRule(gate, rule);
}

// Uses an ExactMatchConfig to restore this module's runtime config.
// If this returns with an error, the state may be partially restored.
// TODO(torek): consider vetting the entire argument before clobbering state.
CommandResponse ExactMatch::SetRuntimeConfig(
    const bess::pb::ExactMatchConfig &arg) {
  default_gate_ = arg.default_gate();
  table_.ClearRules();

  for (auto i = 0; i < arg.rules_size(); i++) {
    Error ret = AddRule(arg.rules(i));
    if (ret.first) {
      return CommandFailure(ret.first, "%s", ret.second.c_str());
    }
  }
  return CommandSuccess();
}

void ExactMatch::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  gate_idx_t default_gate;
  ExactMatchKey keys[bess::PacketBatch::kMaxBurst] __ymm_aligned;

  default_gate = ACCESS_ONCE(default_gate_);

  const auto buffer_fn = [&](bess::Packet *pkt, const ExactMatchField &f) {
    int attr_id = f.attr_id;
    if (attr_id >= 0) {
      return ptr_attr<uint8_t>(this, attr_id, pkt);
    }
    return pkt->head_data<uint8_t *>() + f.offset;
  };
  table_.MakeKeys(batch, buffer_fn, keys);

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    EmitPacket(ctx, pkt, table_.Find(keys[i], default_gate));
  }
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
  Error ret = AddRule(arg);
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
