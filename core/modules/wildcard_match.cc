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

const Commands<Module> WildcardMatch::cmds = {
    {"add", MODULE_FUNC &WildcardMatch::CommandAdd, 0},
    {"delete", MODULE_FUNC &WildcardMatch::CommandDelete, 0},
    {"clear", MODULE_FUNC &WildcardMatch::CommandClear, 0},
    {"set_default_gate", MODULE_FUNC &WildcardMatch::CommandSetDefaultGate, 1}};

const PbCommands WildcardMatch::pb_cmds = {
    {"add", MODULE_CMD_FUNC(&WildcardMatch::CommandAddPb), 0},
    {"delete", MODULE_CMD_FUNC(&WildcardMatch::CommandDeletePb), 0},
    {"clear", MODULE_CMD_FUNC(&WildcardMatch::CommandClearPb), 0},
    {"set_default_gate",
     MODULE_CMD_FUNC(&WildcardMatch::CommandSetDefaultGatePb), 1}};

struct snobj *WildcardMatch::AddFieldOne(struct snobj *field,
                                         struct WmField *f) {
  if (field->type != TYPE_MAP) {
    return snobj_err(EINVAL, "'fields' must be a list of maps");
  }

  f->size = snobj_eval_uint(field, "size");

  if (f->size < 1 || f->size > MAX_FIELD_SIZE) {
    return snobj_err(EINVAL, "'size' must be 1-%d", MAX_FIELD_SIZE);
  }

  if (snobj_eval_exists(field, "offset")) {
    f->attr_id = -1;
    f->offset = snobj_eval_int(field, "offset");
    if (f->offset < 0 || f->offset > 1024) {
      return snobj_err(EINVAL, "too small 'offset'");
    }
    return nullptr;
  }

  const char *attr = snobj_eval_str(field, "attr");
  if (!attr) {
    return snobj_err(EINVAL, "specify 'offset' or 'attr'");
  }

  f->attr_id = AddMetadataAttr(attr, f->size, Attribute::AccessMode::kRead);
  if (f->attr_id < 0) {
    return snobj_err(-f->attr_id, "add_metadata_attr() failed");
  }

  return nullptr;
}

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
    f->attr_id =
        AddMetadataAttr(attr, f->size, Attribute::AccessMode::kRead);
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
struct snobj *WildcardMatch::Init(struct snobj *arg) {
  int size_acc = 0;

  struct snobj *fields = snobj_eval(arg, "fields");

  if (snobj_type(fields) != TYPE_LIST) {
    return snobj_err(EINVAL, "'fields' must be a list of maps");
  }

  for (size_t i = 0; i < fields->size; i++) {
    struct snobj *field = snobj_list_get(fields, i);
    struct snobj *err;
    struct WmField f;

    f.pos = size_acc;

    err = AddFieldOne(field, &f);
    if (err) {
      return err;
    }

    size_acc += f.size;
    fields_[i] = f;
  }

  default_gate_ = DROP_GATE;
  num_fields_ = fields->size;
  total_key_size_ = align_ceil(size_acc, sizeof(uint64_t));

  return nullptr;
}

pb_error_t WildcardMatch::InitPb(const bess::pb::WildcardMatchArg &arg) {
  int size_acc = 0;

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &field = arg.fields(i);
    pb_error_t err;
    struct WmField f;

    f.pos = size_acc;

    err = AddFieldOne(field, &f);
    if (err.err() != 0) {
      return err;
    }

    size_acc += f.size;
    fields_[i] = f;
  }

  default_gate_ = DROP_GATE;
  num_fields_ = (size_t)arg.fields_size();
  total_key_size_ = align_ceil(size_acc, sizeof(uint64_t));

  return pb_errno(0);
}

void WildcardMatch::Deinit() {
  for (int i = 0; i < num_tuples_; i++) {
    tuples_[i].ht.Close();
  }
}

gate_idx_t WildcardMatch::LookupEntry(wm_hkey_t *key, gate_idx_t def_gate) {
  struct WmData result = {
      .priority = INT_MIN, .ogate = def_gate,
  };

  const int key_size = total_key_size_;
  const int num_tuples = num_tuples_;

  wm_hkey_t key_masked;

  for (int i = 0; i < num_tuples; i++) {
    struct WmTuple *tuple = &tuples_[i];
    struct WmData *cand;

    mask(&key_masked, key, &tuple->mask, key_size);

    cand = static_cast<struct WmData *>(tuple->ht.Get(&key_masked));

    if (cand && cand->priority >= result.priority) {
      result = *cand;
    }
  }

  return result.ogate;
}

void WildcardMatch::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t default_gate;
  gate_idx_t out_gates[MAX_PKT_BURST];

  char keys[MAX_PKT_BURST][HASH_KEY_SIZE] __ymm_aligned;

  int cnt = batch->cnt;

  default_gate = ACCESS_ONCE(default_gate_);

  for (size_t i = 0; i < num_fields_; i++) {
    int offset;
    int pos = fields_[i].pos;
    int attr_id = fields_[i].attr_id;

    if (attr_id < 0) {
      offset = fields_[i].offset;
    } else {
      offset =
          mt_offset_to_databuf_offset(WildcardMatch::attr_offsets[attr_id]);
    }

    char *key = keys[0] + pos;

    for (int j = 0; j < cnt; j++, key += HASH_KEY_SIZE) {
      char *buf_addr = (char *)batch->pkts[j]->mbuf.buf_addr;

      /* for offset-based attrs we use relative offset */
      if (attr_id < 0) {
        buf_addr += batch->pkts[j]->mbuf.data_off;
      }

      *(uint64_t *)key = *(uint64_t *)(buf_addr + offset);
    }
  }

#if 1
  for (int i = 0; i < cnt; i++) {
    out_gates[i] = LookupEntry((wm_hkey_t *)keys[i], default_gate);
  }
#else
  /* A version with an outer loop for tuples and an inner loop for pkts.
   * Significantly slower. */

  int priorities[MAX_PKT_BURST];
  const int key_size = total_key_size_;

  for (int i = 0; i < cnt; i++) {
    priorities[i] = INT_MIN;
    out_gates[i] = default_gate;
  }

  for (int i = 0; i < num_tuples_; i++) {
    const struct WmTuple *tuple = &tuples_[i];
    const struct htable *ht = &tuple->ht;
    const wm_hkey_t *tuple_mask = &tuple->mask;

    for (int j = 0; j < cnt; j++) {
      wm_hkey_t key_masked;
      struct WmData *cand;

      mask(&key_masked, keys[j], tuple_mask, key_size);

      cand = ht->Get(&key_masked);

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

  for (int i = 0; i < num_tuples_; i++) {
    num_rules += tuples_[i].ht.Count();
  }

  return bess::utils::Format("%lu fields, %d rules", num_fields_, num_rules);
}

struct snobj *WildcardMatch::GetDump() const {
  struct snobj *r = snobj_map();
  struct snobj *fields = snobj_list();
  struct snobj *rules = snobj_list();

  for (size_t i = 0; i < num_fields_; i++) {
    struct snobj *f_obj = snobj_map();
    const struct WmField *f = &fields_[i];

    snobj_map_set(f_obj, "size", snobj_uint(f->size));

    if (f->attr_id < 0) {
      snobj_map_set(f_obj, "offset", snobj_uint(f->offset));
    } else {
      snobj_map_set(f_obj, "name",
                    snobj_str(WildcardMatch::all_attrs()[f->attr_id].name));
    }

    snobj_list_add(fields, f_obj);
  }

  for (int k = 0; k < num_tuples_; k++) {
    const struct WmTuple *tuple = &tuples_[k];

    CollectRules(tuple, rules);
  }

  snobj_map_set(r, "fields", fields);
  snobj_map_set(r, "rules", rules);

  return r;
}

void WildcardMatch::CollectRules(const struct WmTuple *tuple,
                                 struct snobj *rules) const {
  uint32_t next = 0;
  void *key;
  const void *mask = &tuple->mask;

  while ((key = tuple->ht.Iterate(&next))) {
    struct snobj *rule = snobj_map();
    struct snobj *values = snobj_list();
    struct snobj *masks = snobj_list();

    for (size_t i = 0; i < num_fields_; i++) {
      const struct WmField *f = &fields_[i];
      int pos = f->pos;
      int size = f->size;

      snobj_list_add(values,
                     snobj_blob(static_cast<uint8_t *>(key) + pos, size));
      snobj_list_add(
          masks,
          snobj_blob(reinterpret_cast<const uint8_t *>(mask) + pos, size));
    }

    snobj_map_set(rule, "values", values);
    snobj_map_set(rule, "masks", masks);
    snobj_list_add(rules, rule);
  }
}

template <typename T>
pb_error_t WildcardMatch::ExtractKeyMask(const T &arg, wm_hkey_t *key,
                                         wm_hkey_t *mask) {
  if ((size_t)arg.values_size() != num_fields_) {
    return pb_error(EINVAL, "must specify %lu values", num_fields_);
  } else if ((size_t)arg.masks_size() != num_fields_) {
    return pb_error(EINVAL, "must specify %lu masks", num_fields_);
  }

  memset(key, 0, sizeof(*key));
  memset(mask, 0, sizeof(*mask));

  for (size_t i = 0; i < num_fields_; i++) {
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

struct snobj *WildcardMatch::ExtractKeyMask(struct snobj *arg, wm_hkey_t *key,
                                            wm_hkey_t *mask) {
  struct snobj *values;
  struct snobj *masks;

  if (snobj_type(arg) != TYPE_MAP) {
    return snobj_err(EINVAL, "argument must be a map");
  }

  values = snobj_eval(arg, "values");
  masks = snobj_eval(arg, "masks");

  if (!values || snobj_type(values) != TYPE_LIST || !snobj_size(values)) {
    return snobj_err(EINVAL, "'values' must be a list");
  } else if (values->size != num_fields_) {
    return snobj_err(EINVAL, "must specify %lu values", num_fields_);
  } else if (!masks || snobj_type(masks) != TYPE_LIST) {
    return snobj_err(EINVAL, "'masks' must be a list");
  } else if (masks->size != num_fields_) {
    return snobj_err(EINVAL, "must specify %lu masks", num_fields_);
  }

  memset(key, 0, sizeof(*key));
  memset(mask, 0, sizeof(*mask));

  for (size_t i = 0; i < values->size; i++) {
    int field_size = fields_[i].size;
    int field_pos = fields_[i].pos;

    struct snobj *v_obj = snobj_list_get(values, i);
    struct snobj *m_obj = snobj_list_get(masks, i);
    uint64_t v = 0;
    uint64_t m = 0;

    int force_be = (fields_[i].attr_id < 0);

    if (snobj_binvalue_get(v_obj, field_size, &v, force_be)) {
      return snobj_err(EINVAL, "idx %lu: not a correct %d-byte value", i,
                       field_size);
    } else if (snobj_binvalue_get(m_obj, field_size, &m, force_be)) {
      return snobj_err(EINVAL, "idx %lu: not a correct %d-byte mask", i,
                       field_size);
    }

    if (v & ~m) {
      return snobj_err(EINVAL,
                       "idx %lu: invalid pair of "
                       "value 0x%0*" PRIx64
                       " and "
                       "mask 0x%0*" PRIx64,
                       i, field_size * 2, v, field_size * 2, m);
    }

    memcpy(reinterpret_cast<uint8_t *>(key) + field_pos, &v, field_size);
    memcpy(reinterpret_cast<uint8_t *>(mask) + field_pos, &m, field_size);
  }

  return nullptr;
}

int WildcardMatch::FindTuple(wm_hkey_t *mask) {
  int key_size = total_key_size_;

  for (int i = 0; i < num_tuples_; i++) {
    struct WmTuple *tuple = &tuples_[i];

    if (memcmp(&tuple->mask, mask, key_size) == 0) {
      return i;
    }
  }

  return -ENOENT;
}

int WildcardMatch::AddTuple(wm_hkey_t *mask) {
  struct WmTuple *tuple;

  int ret;

  if (num_tuples_ >= MAX_TUPLES) {
    return -ENOSPC;
  }

  tuple = &tuples_[num_tuples_++];
  memcpy(&tuple->mask, mask, sizeof(*mask));

  ret = tuple->ht.Init(total_key_size_, sizeof(struct WmData));
  if (ret < 0) {
    return ret;
  }

  return tuple - tuples_;
}

int WildcardMatch::AddEntry(struct WmTuple *tuple, wm_hkey_t *key,
                            struct WmData *data) {
  int ret;

  ret = tuple->ht.Set(key, data);
  if (ret < 0) {
    return ret;
  }

  return 0;
}

int WildcardMatch::DelEntry(struct WmTuple *tuple, wm_hkey_t *key) {
  int ret = tuple->ht.Del(key);
  if (ret) {
    return ret;
  }

  if (tuple->ht.Count() == 0) {
    int idx = tuple - tuples_;

    tuple->ht.Close();

    num_tuples_--;
    memmove(&tuples_[idx], &tuples_[idx + 1],
            sizeof(*tuple) * (num_tuples_ - idx));
  }

  return 0;
}

pb_cmd_response_t WildcardMatch::CommandAddPb(
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

  int ret = AddEntry(&tuples_[idx], &key, &data);
  if (ret < 0) {
    set_cmd_response_error(&response, pb_error(-ret, "failed to add a rule"));
    return response;
  }

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t WildcardMatch::CommandDeletePb(
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

  int ret = DelEntry(&tuples_[idx], &key);
  if (ret < 0) {
    set_cmd_response_error(&response,
                           pb_error(-ret, "failed to delete a rule"));
    return response;
  }

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t WildcardMatch::CommandClearPb(const bess::pb::EmptyArg &) {
  for (int i = 0; i < num_tuples_; i++) {
    tuples_[i].ht.Clear();
  }

  pb_cmd_response_t response;

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t WildcardMatch::CommandSetDefaultGatePb(
    const bess::pb::WildcardMatchCommandSetDefaultGateArg &arg) {
  pb_cmd_response_t response;

  int gate = arg.gate();

  default_gate_ = gate;

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct snobj *WildcardMatch::CommandAdd(struct snobj *arg) {
  gate_idx_t gate = snobj_eval_uint(arg, "gate");
  int priority = snobj_eval_int(arg, "priority");

  wm_hkey_t key;
  wm_hkey_t mask;

  struct WmData data;

  struct snobj *err = ExtractKeyMask(arg, &key, &mask);
  if (err) {
    return err;
  }

  if (!snobj_eval_exists(arg, "gate")) {
    return snobj_err(EINVAL, "'gate' must be specified");
  }

  if (!is_valid_gate(gate)) {
    return snobj_err(EINVAL, "Invalid gate: %hu", gate);
  }

  data.priority = priority;
  data.ogate = gate;

  int idx = FindTuple(&mask);
  if (idx < 0) {
    idx = AddTuple(&mask);
    if (idx < 0) {
      return snobj_err(-idx, "failed to add a new wildcard pattern");
    }
  }

  int ret = AddEntry(&tuples_[idx], &key, &data);
  if (ret < 0) {
    return snobj_err(-ret, "failed to add a rule");
  }

  return nullptr;
}

struct snobj *WildcardMatch::CommandDelete(struct snobj *arg) {
  wm_hkey_t key;
  wm_hkey_t mask;

  struct snobj *err = ExtractKeyMask(arg, &key, &mask);
  if (err) {
    return err;
  }

  int idx = FindTuple(&mask);
  if (idx < 0) {
    return snobj_err(-idx, "failed to delete a rule");
  }

  int ret = DelEntry(&tuples_[idx], &key);
  if (ret < 0) {
    return snobj_err(-ret, "failed to delete a rule");
  }

  return nullptr;
}

struct snobj *WildcardMatch::CommandClear(struct snobj *) {
  for (int i = 0; i < num_tuples_; i++) {
    tuples_[i].ht.Clear();
  }

  return nullptr;
}

struct snobj *WildcardMatch::CommandSetDefaultGate(struct snobj *arg) {
  int gate = snobj_int_get(arg);

  default_gate_ = gate;

  return nullptr;
}

ADD_MODULE(WildcardMatch, "wm",
           "Multi-field classifier with a wildcard match table")
