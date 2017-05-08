#include "wildcard_match.h"

#include <string>
#include <vector>

#include "../utils/endian.h"
#include "../utils/format.h"

using bess::metadata::Attribute;

// dst = src & mask. len must be a multiple of sizeof(uint64_t)
static inline void mask(wm_hkey_t *dst, const wm_hkey_t &src,
                        const wm_hkey_t &mask, size_t len) {
  promise(len >= sizeof(uint64_t));
  promise(len <= sizeof(wm_hkey_t));

  for (size_t i = 0; i < len / 8; i++) {
    dst->u64_arr[i] = src.u64_arr[i] & mask.u64_arr[i];
  }
}

// XXX: this is repeated in many modules. get rid of them when converting .h to
// .hh, etc... it's in defined in some old header
static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

const Commands WildcardMatch::cmds = {
    {"add", "WildcardMatchCommandAddArg",
     MODULE_CMD_FUNC(&WildcardMatch::CommandAdd), 0},
    {"delete", "WildcardMatchCommandDeleteArg",
     MODULE_CMD_FUNC(&WildcardMatch::CommandDelete), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&WildcardMatch::CommandClear), 0},
    {"get_rules", "EmptyArg", MODULE_CMD_FUNC(&WildcardMatch::CommandGetRules),
     0},
    {"set_default_gate", "WildcardMatchCommandSetDefaultGateArg",
     MODULE_CMD_FUNC(&WildcardMatch::CommandSetDefaultGate), 1}};

CommandResponse WildcardMatch::AddFieldOne(
    const bess::pb::WildcardMatchField &field, struct WmField *f) {
  f->size = field.size();

  if (f->size < 1 || f->size > MAX_FIELD_SIZE) {
    return CommandFailure(EINVAL, "'size' must be 1-%d", MAX_FIELD_SIZE);
  }

  if (field.position_case() == bess::pb::WildcardMatchField::kOffset) {
    f->attr_id = -1;
    f->offset = field.offset();
    if (f->offset < 0 || f->offset > 1024) {
      return CommandFailure(EINVAL, "too small 'offset'");
    }
  } else if (field.position_case() ==
             bess::pb::WildcardMatchField::kAttribute) {
    const char *attr = field.attribute().c_str();
    f->attr_id = AddMetadataAttr(attr, f->size, Attribute::AccessMode::kRead);
    if (f->attr_id < 0) {
      return CommandFailure(-f->attr_id, "add_metadata_attr() failed");
    }
  } else {
    return CommandFailure(EINVAL, "specify 'offset' or 'attr'");
  }

  return CommandSuccess();
}

/* Takes a list of all fields that may be used by rules.
 * Each field needs 'offset' (or 'name') and 'size' in bytes,
 *
 * e.g.: WildcardMatch([{'offset': 26, 'size': 4}, ...]
 * (checks the source IP address)
 *
 * You can also specify metadata attributes
 * e.g.: WildcardMatch([{'name': 'nexthop', 'size': 4}, ...] */

CommandResponse WildcardMatch::Init(const bess::pb::WildcardMatchArg &arg) {
  int size_acc = 0;

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &field = arg.fields(i);
    CommandResponse err;
    fields_.emplace_back();
    struct WmField &f = fields_.back();

    f.pos = size_acc;

    err = AddFieldOne(field, &f);
    if (err.error().code() != 0) {
      return err;
    }

    size_acc += f.size;
  }

  default_gate_ = DROP_GATE;
  total_key_size_ = align_ceil(size_acc, sizeof(uint64_t));

  return CommandSuccess();
}

inline gate_idx_t WildcardMatch::LookupEntry(const wm_hkey_t &key,
                                             gate_idx_t def_gate) {
  struct WmData result = {
      .priority = INT_MIN, .ogate = def_gate,
  };

  for (auto &tuple : tuples_) {
    const auto &ht = tuple.ht;
    wm_hkey_t key_masked;

    mask(&key_masked, key, tuple.mask, total_key_size_);

    const auto *entry =
        ht.Find(key_masked, wm_hash(total_key_size_), wm_eq(total_key_size_));

    if (entry && entry->second.priority >= result.priority) {
      result = entry->second;
    }
  }

  return result.ogate;
}

void WildcardMatch::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t default_gate;
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];

  wm_hkey_t keys[bess::PacketBatch::kMaxBurst] __ymm_aligned;

  int cnt = batch->cnt();

  // Initialize the padding with zero
  for (int i = 0; i < cnt; i++) {
    keys[i].u64_arr[(total_key_size_ - 1) / 8] = 0;
  }

  default_gate = ACCESS_ONCE(default_gate_);

  for (const auto &field : fields_) {
    int offset;
    int pos = field.pos;
    int attr_id = field.attr_id;

    if (attr_id < 0) {
      offset = field.offset;
    } else {
      offset = bess::Packet::mt_offset_to_databuf_offset(attr_offset(attr_id));
    }

    for (int j = 0; j < cnt; j++) {
      char *buf_addr = batch->pkts()[j]->buffer<char *>();

      /* for offset-based attrs we use relative offset */
      if (attr_id < 0) {
        buf_addr += batch->pkts()[j]->data_off();
      }

      char *key = reinterpret_cast<char *>(keys[j].u64_arr) + pos;

      *(reinterpret_cast<uint64_t *>(key)) =
          *(reinterpret_cast<uint64_t *>(buf_addr + offset));
    }
  }

  for (int i = 0; i < cnt; i++) {
    out_gates[i] = LookupEntry(keys[i], default_gate);
  }

  RunSplit(out_gates, batch);
}

std::string WildcardMatch::GetDesc() const {
  int num_rules = 0;

  for (const auto &tuple : tuples_) {
    num_rules += tuple.ht.Count();
  }

  return bess::utils::Format("%zu fields, %d rules", fields_.size(), num_rules);
}

template <typename T>
CommandResponse WildcardMatch::ExtractKeyMask(const T &arg, wm_hkey_t *key,
                                              wm_hkey_t *mask) {
  if ((size_t)arg.values_size() != fields_.size()) {
    return CommandFailure(EINVAL, "must specify %zu values", fields_.size());
  } else if ((size_t)arg.masks_size() != fields_.size()) {
    return CommandFailure(EINVAL, "must specify %zu masks", fields_.size());
  }

  memset(key, 0, sizeof(*key));
  memset(mask, 0, sizeof(*mask));

  for (size_t i = 0; i < fields_.size(); i++) {
    int field_size = fields_[i].size;
    int field_pos = fields_[i].pos;

    uint64_t v = 0;
    uint64_t m = 0;

    bool force_be = (fields_[i].attr_id < 0);

    if (!bess::utils::uint64_to_bin(&v, arg.values(i), field_size,
                                    force_be || bess::utils::is_be_system())) {
      return CommandFailure(EINVAL, "idx %zu: not a correct %d-byte value", i,
                            field_size);
    } else if (!bess::utils::uint64_to_bin(
                   &m, arg.masks(i), field_size,
                   force_be || bess::utils::is_be_system())) {
      return CommandFailure(EINVAL, "idx %zu: not a correct %d-byte mask", i,
                            field_size);
    }

    if (v & ~m) {
      return CommandFailure(EINVAL,
                            "idx %zu: invalid pair of "
                            "value 0x%0*" PRIx64
                            " and "
                            "mask 0x%0*" PRIx64,
                            i, field_size * 2, v, field_size * 2, m);
    }

    bess::utils::Copy(reinterpret_cast<uint8_t *>(key) + field_pos, &v,
                      field_size);
    bess::utils::Copy(reinterpret_cast<uint8_t *>(mask) + field_pos, &m,
                      field_size);
  }

  return CommandSuccess();
}

int WildcardMatch::FindTuple(wm_hkey_t *mask) {
  int i = 0;

  for (const auto &tuple : tuples_) {
    if (memcmp(&tuple.mask, mask, total_key_size_) == 0) {
      return i;
    }
    i++;
  }
  return -ENOENT;
}

int WildcardMatch::AddTuple(wm_hkey_t *mask) {
  if (tuples_.size() >= MAX_TUPLES) {
    return -ENOSPC;
  }

  tuples_.emplace_back();
  struct WmTuple &tuple = tuples_.back();
  bess::utils::Copy(&tuple.mask, mask, sizeof(*mask));

  return int(tuples_.size() - 1);
}

int WildcardMatch::DelEntry(int idx, wm_hkey_t *key) {
  struct WmTuple &tuple = tuples_[idx];
  int ret =
      tuple.ht.Remove(*key, wm_hash(total_key_size_), wm_eq(total_key_size_));
  if (ret) {
    return ret;
  }

  if (tuple.ht.Count() == 0) {
    tuples_.erase(tuples_.begin() + idx);
  }

  return 0;
}

CommandResponse WildcardMatch::CommandAdd(
    const bess::pb::WildcardMatchCommandAddArg &arg) {
  gate_idx_t gate = arg.gate();
  int priority = arg.priority();

  wm_hkey_t key = {{0}};
  wm_hkey_t mask = {{0}};

  struct WmData data;

  CommandResponse err = ExtractKeyMask(arg, &key, &mask);
  if (err.error().code() != 0) {
    return err;
  }

  if (!is_valid_gate(gate)) {
    return CommandFailure(EINVAL, "Invalid gate: %hu", gate);
  }

  data.priority = priority;
  data.ogate = gate;

  int idx = FindTuple(&mask);
  if (idx < 0) {
    idx = AddTuple(&mask);
    if (idx < 0) {
      return CommandFailure(-idx, "failed to add a new wildcard pattern");
    }
  }

  auto *ret = tuples_[idx].ht.Insert(key, data, wm_hash(total_key_size_),
                                     wm_eq(total_key_size_));
  if (ret == nullptr) {
    return CommandFailure(EINVAL, "failed to add a rule");
  }

  return CommandSuccess();
}

CommandResponse WildcardMatch::CommandDelete(
    const bess::pb::WildcardMatchCommandDeleteArg &arg) {
  wm_hkey_t key;
  wm_hkey_t mask;

  CommandResponse err = ExtractKeyMask(arg, &key, &mask);
  if (err.error().code() != 0) {
    return err;
  }

  int idx = FindTuple(&mask);
  if (idx < 0) {
    return CommandFailure(-idx, "failed to delete a rule");
  }

  int ret = DelEntry(idx, &key);
  if (ret < 0) {
    return CommandFailure(-ret, "failed to delete a rule");
  }

  return CommandSuccess();
}

CommandResponse WildcardMatch::CommandClear(const bess::pb::EmptyArg &) {
  for (auto &tuple : tuples_) {
    tuple.ht.Clear();
  }

  CommandResponse response;

  return CommandSuccess();
}

CommandResponse WildcardMatch::CommandGetRules(const bess::pb::EmptyArg &) {
  bess::pb::WildcardMatchCommandGetRulesResponse resp;
  resp.set_default_gate(default_gate_);

  for (auto &field : fields_) {
    bess::pb::WildcardMatchField *f = resp.add_fields();
    if (field.attr_id >= 0) {
      f->set_attribute(all_attrs().at(field.attr_id).name);
    } else {
      f->set_offset(field.offset);
    }
    f->set_size(field.size);
  }

  for (auto &tuple : tuples_) {
    wm_hkey_t mask = tuple.mask;
    for (auto &entry : tuple.ht) {
      bess::pb::WildcardMatchRule *rule = resp.add_rules();
      rule->set_priority(entry.second.priority);
      rule->set_gate(entry.second.ogate);

      uint8_t *entry_data = reinterpret_cast<uint8_t *>(entry.first.u64_arr);
      uint8_t *entry_mask = reinterpret_cast<uint8_t *>(mask.u64_arr);
      for (auto &field : fields_) {
        uint64_t data = 0;
        bess::utils::bin_to_uint64(&data, entry_data + field.pos, field.size,
                                   1);
        rule->add_values(data);
        uint64_t mask_data = 0;
        bess::utils::bin_to_uint64(&mask_data, entry_mask + field.pos,
                                   field.size, 1);
        rule->add_masks(mask_data);
      }
    }
  }

  return CommandSuccess(resp);
}

CommandResponse WildcardMatch::CommandSetDefaultGate(
    const bess::pb::WildcardMatchCommandSetDefaultGateArg &arg) {
  default_gate_ = arg.gate();
  return CommandSuccess();
}

ADD_MODULE(WildcardMatch, "wm",
           "Multi-field classifier with a wildcard match table")
