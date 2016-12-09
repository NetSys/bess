#include "wildcard_match.h"

#include "../utils/endian.h"
#include "../utils/format.h"

using bess::metadata::Attribute;

/* k1 = k2 & mask */
static void mask(void *k1, const void *k2, const void *mask, int key_size) {
  uint64_t *a = static_cast<uint64_t *>(k1);
  const uint64_t *b = reinterpret_cast<const uint64_t *>(k2);
  const uint64_t *m = reinterpret_cast<const uint64_t *>(mask);

  switch (key_size >> 3) {
    default:
      promise_unreachable();
    case 8:
      a[7] = b[7] & m[7];
    case 7:
      a[6] = b[6] & m[6];
    case 6:
      a[5] = b[5] & m[5];
    case 5:
      a[4] = b[4] & m[4];
    case 4:
      a[3] = b[3] & m[3];
    case 3:
      a[2] = b[2] & m[2];
    case 2:
      a[1] = b[1] & m[1];
    case 1:
      a[0] = b[0] & m[0];
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
    {"set_default_gate", "WildcardMatchCommandSetDefaultGateArg",
     MODULE_CMD_FUNC(&WildcardMatch::CommandSetDefaultGate), 1}};

pb_error_t WildcardMatch::AddFieldOne(
    const bess::pb::WildcardMatchArg_Field &field, struct WmField *f) {
  f->size = field.size();

  if (f->size < 1 || f->size > MAX_FIELD_SIZE) {
    return pb_error(EINVAL, "'size' must be 1-%d", MAX_FIELD_SIZE);
  }

  if (field.length_case() == bess::pb::WildcardMatchArg_Field::kOffset) {
    f->attr_id = -1;
    f->offset = field.offset();
    if (f->offset < 0 || f->offset > 1024) {
      return pb_error(EINVAL, "too small 'offset'");
    }
  } else if (field.length_case() ==
             bess::pb::WildcardMatchArg_Field::kAttribute) {
    const char *attr = field.attribute().c_str();
    f->attr_id = AddMetadataAttr(attr, f->size, Attribute::AccessMode::kRead);
    if (f->attr_id < 0) {
      return pb_error(-f->attr_id, "add_metadata_attr() failed");
    }
  } else {
    return pb_error(EINVAL, "specify 'offset' or 'attr'");
  }

  return pb_errno(0);
}

/* Takes a list of all fields that may be used by rules.
 * Each field needs 'offset' (or 'name') and 'size' in bytes,
 *
 * e.g.: WildcardMatch([{'offset': 26, 'size': 4}, ...]
 * (checks the source IP address)
 *
 * You can also specify metadata attributes
 * e.g.: WildcardMatch([{'name': 'nexthop', 'size': 4}, ...] */

pb_error_t WildcardMatch::Init(const bess::pb::WildcardMatchArg &arg) {
  int size_acc = 0;

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &field = arg.fields(i);
    pb_error_t err;
    fields_.emplace_back();
    struct WmField &f = fields_.back();

    f.pos = size_acc;

    err = AddFieldOne(field, &f);
    if (err.err() != 0) {
      return err;
    }

    size_acc += f.size;
  }

  default_gate_ = DROP_GATE;
  total_key_size_ = align_ceil(size_acc, sizeof(uint64_t));

  return pb_errno(0);
}

void WildcardMatch::DeInit() {
  for (auto &tuple : tuples_) {
    tuple.ht.Close();
  }
}

gate_idx_t WildcardMatch::LookupEntry(wm_hkey_t *key, gate_idx_t def_gate) {
  struct WmData result = {
      .priority = INT_MIN, .ogate = def_gate,
  };

  const int key_size = total_key_size_;

  wm_hkey_t key_masked;

  for (const auto &tuple : tuples_) {
    struct WmData *cand;

    mask(&key_masked, key, &tuple.mask, key_size);

    cand = static_cast<struct WmData *>(tuple.ht.Get(&key_masked));

    if (cand && cand->priority >= result.priority) {
      result = *cand;
    }
  }

  return result.ogate;
}

void WildcardMatch::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t default_gate;
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];

  char keys[bess::PacketBatch::kMaxBurst][HASH_KEY_SIZE] __ymm_aligned;

  int cnt = batch->cnt();

  default_gate = ACCESS_ONCE(default_gate_);

  for (const auto &field : fields_) {
    int offset;
    int pos = field.pos;
    int attr_id = field.attr_id;

    if (attr_id < 0) {
      offset = field.offset;
    } else {
      offset = bess::Packet::mt_offset_to_databuf_offset(
          WildcardMatch::attr_offsets[attr_id]);
    }

    char *key = keys[0] + pos;

    for (int j = 0; j < cnt; j++, key += HASH_KEY_SIZE) {
      char *buf_addr = batch->pkts()[j]->buffer<char *>();

      /* for offset-based attrs we use relative offset */
      if (attr_id < 0) {
        buf_addr += batch->pkts()[j]->data_off();
      }

      *(reinterpret_cast<uint64_t *>(key)) =
          *(reinterpret_cast<uint64_t *>(buf_addr + offset));
    }
  }

#if 1
  for (int i = 0; i < cnt; i++) {
    out_gates[i] =
        LookupEntry(reinterpret_cast<wm_hkey_t *>(keys[i]), default_gate);
  }
#else
  /* A version with an outer loop for tuples and an inner loop for pkts.
   * Significantly slower. */

  int priorities[bess::PacketBatch::kMaxBurst];
  const int key_size = total_key_size_;

  for (int i = 0; i < cnt; i++) {
    priorities[i] = INT_MIN;
    out_gates[i] = default_gate;
  }

  for (const auto &tuple : tuples_) {
    const wm_hkey_t *tuple_mask = &tuple.mask;

    for (int j = 0; j < cnt; j++) {
      wm_hkey_t key_masked;
      struct WmData *cand;

      mask(&key_masked, keys[j], tuple_mask, key_size);

      cand = tuple.ht.Get(&key_masked);

      if (cand && cand->priority >= priorities[j]) {
        out_gates[j] = cand->ogate;
        priorities[j] = cand->priority;
      }
    }
  }
#endif

  RunSplit(out_gates, batch);
}

std::string WildcardMatch::GetDesc() const {
  int num_rules = 0;

  for (const auto &tuple : tuples_) {
    num_rules += tuple.ht.Count();
  }

  return bess::utils::Format("%lu fields, %d rules", fields_.size(), num_rules);
}

template <typename T>
pb_error_t WildcardMatch::ExtractKeyMask(const T &arg, wm_hkey_t *key,
                                         wm_hkey_t *mask) {
  if ((size_t)arg.values_size() != fields_.size()) {
    return pb_error(EINVAL, "must specify %lu values", fields_.size());
  } else if ((size_t)arg.masks_size() != fields_.size()) {
    return pb_error(EINVAL, "must specify %lu masks", fields_.size());
  }

  memset(key, 0, sizeof(*key));
  memset(mask, 0, sizeof(*mask));

  for (size_t i = 0; i < fields_.size(); i++) {
    int field_size = fields_[i].size;
    int field_pos = fields_[i].pos;

    uint64_t v = 0;
    uint64_t m = 0;

    int force_be = (fields_[i].attr_id < 0);

    if (uint64_to_bin(reinterpret_cast<uint8_t *>(&v), field_size,
                      arg.values(i), force_be || bess::utils::is_be_system())) {
      return pb_error(EINVAL, "idx %lu: not a correct %d-byte value", i,
                      field_size);
    } else if (uint64_to_bin(reinterpret_cast<uint8_t *>(&m), field_size,
                             arg.masks(i),
                             force_be || bess::utils::is_be_system())) {
      return pb_error(EINVAL, "idx %lu: not a correct %d-byte mask", i,
                      field_size);
    }

    if (v & ~m) {
      return pb_error(EINVAL,
                      "idx %lu: invalid pair of "
                      "value 0x%0*" PRIx64
                      " and "
                      "mask 0x%0*" PRIx64,
                      i, field_size * 2, v, field_size * 2, m);
    }

    memcpy(reinterpret_cast<uint8_t *>(key) + field_pos, &v, field_size);
    memcpy(reinterpret_cast<uint8_t *>(mask) + field_pos, &m, field_size);
  }

  return pb_errno(0);
}

int WildcardMatch::FindTuple(wm_hkey_t *mask) {
  int key_size = total_key_size_;
  int i = 0;

  for (const auto &tuple : tuples_) {
    if (memcmp(&tuple.mask, mask, key_size) == 0) {
      return i;
    }
    i++;
  }

  return -ENOENT;
}

int WildcardMatch::AddTuple(wm_hkey_t *mask) {
  int ret;

  if (tuples_.size() >= MAX_TUPLES) {
    return -ENOSPC;
  }

  tuples_.emplace_back();
  struct WmTuple &tuple = tuples_.back();
  memcpy(&tuple.mask, mask, sizeof(*mask));
  ret = tuple.ht.Init(total_key_size_, sizeof(struct WmData));
  if (ret < 0) {
    tuple.ht.Close();
    return ret;
  }

  return int(tuples_.size() - 1);
}

int WildcardMatch::DelEntry(int idx, wm_hkey_t *key) {
  struct WmTuple &tuple = tuples_[idx];
  int ret = tuple.ht.Del(key);
  if (ret) {
    return ret;
  }

  if (tuple.ht.Count() == 0) {
    tuples_.erase(tuples_.begin() + idx);
  }

  return 0;
}

pb_cmd_response_t WildcardMatch::CommandAdd(
    const bess::pb::WildcardMatchCommandAddArg &arg) {
  pb_cmd_response_t response;

  gate_idx_t gate = arg.gate();
  int priority = arg.priority();

  wm_hkey_t key;
  wm_hkey_t mask;

  struct WmData data;

  pb_error_t err = ExtractKeyMask(arg, &key, &mask);
  if (err.err() != 0) {
    set_cmd_response_error(&response, err);
    return response;
  }

  if (!is_valid_gate(gate)) {
    set_cmd_response_error(&response,
                           pb_error(EINVAL, "Invalid gate: %hu", gate));
    return response;
  }

  data.priority = priority;
  data.ogate = gate;

  int idx = FindTuple(&mask);
  if (idx < 0) {
    idx = AddTuple(&mask);
    if (idx < 0) {
      set_cmd_response_error(
          &response, pb_error(-idx, "failed to add a new wildcard pattern"));
      return response;
    }
  }

  int ret = tuples_[idx].ht.Set(&key, &data);
  if (ret < 0) {
    set_cmd_response_error(&response, pb_error(-ret, "failed to add a rule"));
    return response;
  }

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t WildcardMatch::CommandDelete(
    const bess::pb::WildcardMatchCommandDeleteArg &arg) {
  pb_cmd_response_t response;

  wm_hkey_t key;
  wm_hkey_t mask;

  pb_error_t err = ExtractKeyMask(arg, &key, &mask);
  if (err.err() != 0) {
    set_cmd_response_error(&response, err);
    return response;
  }

  int idx = FindTuple(&mask);
  if (idx < 0) {
    set_cmd_response_error(&response,
                           pb_error(-idx, "failed to delete a rule"));
    return response;
  }

  int ret = DelEntry(idx, &key);
  if (ret < 0) {
    set_cmd_response_error(&response,
                           pb_error(-ret, "failed to delete a rule"));
    return response;
  }

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t WildcardMatch::CommandClear(const bess::pb::EmptyArg &) {
  for (auto &tuple : tuples_) {
    tuple.ht.Clear();
  }

  pb_cmd_response_t response;

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t WildcardMatch::CommandSetDefaultGate(
    const bess::pb::WildcardMatchCommandSetDefaultGateArg &arg) {
  pb_cmd_response_t response;

  int gate = arg.gate();

  default_gate_ = gate;

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

ADD_MODULE(WildcardMatch, "wm",
           "Multi-field classifier with a wildcard match table")
