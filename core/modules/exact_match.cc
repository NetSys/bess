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

const Commands<Module> ExactMatch::cmds = {
    {"add", MODULE_FUNC &ExactMatch::CommandAdd, 0},
    {"delete", MODULE_FUNC &ExactMatch::CommandDelete, 0},
    {"clear", MODULE_FUNC &ExactMatch::CommandClear, 0},
    {"set_default_gate", MODULE_FUNC &ExactMatch::CommandSetDefaultGate, 1}};

const PbCommands ExactMatch::pb_cmds = {
    {"add", MODULE_CMD_FUNC(&ExactMatch::CommandAddPb), 0},
    {"delete", MODULE_CMD_FUNC(&ExactMatch::CommandDeletePb), 0},
    {"clear", MODULE_CMD_FUNC(&ExactMatch::CommandClearPb), 0},
    {"set_default_gate", MODULE_CMD_FUNC(&ExactMatch::CommandSetDefaultGatePb),
     1}};

pb_error_t ExactMatch::AddFieldOne(const bess::pb::ExactMatchArg_Field &field,
                                   struct EmField *f, int idx) {
  f->size = field.size();
  if (f->size < 1 || f->size > MAX_FIELD_SIZE) {
    return pb_error(EINVAL, "idx %d: 'size' must be 1-%d", idx, MAX_FIELD_SIZE);
  }

  if (field.position_case() == bess::pb::ExactMatchArg_Field::kName) {
    const char *attr = field.name().c_str();
    f->attr_id =
        AddMetadataAttr(attr, f->size, bess::metadata::AccessMode::READ);
    if (f->attr_id < 0) {
      return pb_error(-f->attr_id, "idx %d: add_metadata_attr() failed", idx);
    }
  } else if (field.position_case() == bess::pb::ExactMatchArg_Field::kOffset) {
    f->attr_id = -1;
    f->offset = field.offset();
    if (f->offset < 0 || f->offset > 1024) {
      return pb_error(EINVAL, "idx %d: invalid 'offset'", idx);
    }
  } else {
    return pb_error(EINVAL, "idx %d: must specify 'offset' or 'attr'", idx);
  }

  int force_be = (f->attr_id < 0);

  if (!field.mask()) {
    /* by default all bits are considered */
    f->mask = ((uint64_t)1 << (f->size * 8)) - 1;
  } else {
    if (uint64_to_bin((uint8_t *)&f->mask, f->size, field.mask(),
                      bess::utils::is_be_system() | force_be))
      return pb_error(EINVAL, "idx %d: not a correct %d-byte mask", idx,
                      f->size);
  }

  if (f->mask == 0) {
    return pb_error(EINVAL, "idx %d: empty mask", idx);
  }

  return pb_errno(0);
}

struct snobj *ExactMatch::AddFieldOne(struct snobj *field, struct EmField *f,
                                      int idx) {
  if (field->type != TYPE_MAP) {
    return snobj_err(EINVAL, "'fields' must be a list of maps");
  }

  f->size = snobj_eval_uint(field, "size");
  if (f->size < 1 || f->size > MAX_FIELD_SIZE) {
    return snobj_err(EINVAL, "idx %d: 'size' must be 1-%d", idx,
                     MAX_FIELD_SIZE);
  }

  const char *attr = static_cast<char *>(snobj_eval_str(field, "attr"));

  if (attr) {
    f->attr_id =
        AddMetadataAttr(attr, f->size, bess::metadata::AccessMode::READ);
    if (f->attr_id < 0) {
      return snobj_err(-f->attr_id, "idx %d: add_metadata_attr() failed", idx);
    }
  } else if (snobj_eval_exists(field, "offset")) {
    f->attr_id = -1;
    f->offset = snobj_eval_int(field, "offset");
    if (f->offset < 0 || f->offset > 1024) {
      return snobj_err(EINVAL, "idx %d: invalid 'offset'", idx);
    }
  } else {
    return snobj_err(EINVAL, "idx %d: must specify 'offset' or 'attr'", idx);
  }

  struct snobj *mask = snobj_eval(field, "mask");
  int force_be = (f->attr_id < 0);

  if (!mask) {
    /* by default all bits are considered */
    f->mask = ((uint64_t)1 << (f->size * 8)) - 1;
  } else if (snobj_binvalue_get(mask, f->size, &f->mask, force_be)) {
    return snobj_err(EINVAL, "idx %d: not a correct %d-byte mask", idx,
                     f->size);
  }

  if (f->mask == 0) {
    return snobj_err(EINVAL, "idx %d: empty mask", idx);
  }

  return nullptr;
}

/* Takes a list of fields. Each field needs 'offset' (or 'name') and 'size',
 * and optional "mask" (0xfffff.. by default)
 *
 * e.g.: ExactMatch([{'offset': 14, 'size': 1, 'mask':0xf0}, ...]
 * (checks the IP version field)
 *
 * You can also specify metadata attributes
 * e.g.: ExactMatch([{'name': 'nexthop', 'size': 4}, ...] */
struct snobj *ExactMatch::Init(struct snobj *arg) {
  int size_acc = 0;

  struct snobj *fields = snobj_eval(arg, "fields");

  if (snobj_type(fields) != TYPE_LIST) {
    return snobj_err(EINVAL, "'fields' must be a list of maps");
  }

  for (size_t i = 0; i < fields->size; i++) {
    struct snobj *field = snobj_list_get(fields, i);
    struct snobj *err;
    struct EmField *f = &fields_[i];

    f->pos = size_acc;

    err = AddFieldOne(field, f, i);
    if (err)
      return err;

    size_acc += f->size;
  }

  default_gate_ = DROP_GATE;
  num_fields_ = fields->size;
  total_key_size_ = align_ceil(size_acc, sizeof(uint64_t));

  int ret = ht_.Init(total_key_size_, sizeof(gate_idx_t));
  if (ret < 0) {
    return snobj_err(-ret, "hash table creation failed");
  }

  return nullptr;
}

pb_error_t ExactMatch::InitPb(const bess::pb::ExactMatchArg &arg) {
  int size_acc = 0;

  for (auto i = 0; i < arg.fields_size(); ++i) {
    pb_error_t err;
    struct EmField *f = &fields_[i];

    f->pos = size_acc;

    err = AddFieldOne(arg.fields(i), f, i);
    if (err.err() != 0)
      return err;

    size_acc += f->size;
  }

  default_gate_ = DROP_GATE;
  num_fields_ = arg.fields_size();
  total_key_size_ = align_ceil(size_acc, sizeof(uint64_t));

  int ret = ht_.Init(total_key_size_, sizeof(gate_idx_t));
  if (ret < 0) {
    return pb_error(-ret, "hash table creation failed");
  }

  return pb_errno(0);
}

void ExactMatch::Deinit() {
  ht_.Close();
}

void ExactMatch::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t default_gate;
  gate_idx_t out_gates[MAX_PKT_BURST];

  int key_size = total_key_size_;
  char keys[MAX_PKT_BURST][HASH_KEY_SIZE] __ymm_aligned;

  int cnt = batch->cnt;

  default_gate = ACCESS_ONCE(default_gate_);

  for (int i = 0; i < cnt; i++) {
    memset(&keys[i][key_size - 8], 0, sizeof(uint64_t));
  }

  for (int i = 0; i < num_fields_; i++) {
    uint64_t mask = fields_[i].mask;
    int offset;
    int pos = fields_[i].pos;
    int attr_id = fields_[i].attr_id;

    if (attr_id < 0) {
      offset = fields_[i].offset;
    } else {
      offset = mt_offset_to_databuf_offset(attr_offsets[attr_id]);
    }

    char *key = keys[0] + pos;

    for (int j = 0; j < cnt; j++, key += HASH_KEY_SIZE) {
      char *buf_addr = reinterpret_cast<char *>(batch->pkts[j]->mbuf.buf_addr);

      /* for offset-based attrs we use relative offset */
      if (attr_id < 0) {
        buf_addr += batch->pkts[j]->mbuf.data_off;
      }

      *(uint64_t *)key = *(uint64_t *)(buf_addr + offset) & mask;
    }
  }

  for (int i = 0; i < cnt; i++) {
    gate_idx_t *ret = static_cast<gate_idx_t *>(
        ht_.Get(reinterpret_cast<em_hkey_t *>(keys[i])));
    out_gates[i] = ret ? *ret : default_gate;
  }

  RunSplit(out_gates, batch);
}

std::string ExactMatch::GetDesc() const {
  return bess::utils::Format("%d fields, %d rules", num_fields_, ht_.Count());
}

struct snobj *ExactMatch::GetDump() const {
  struct snobj *r = snobj_map();
  struct snobj *fields = snobj_list();
  struct snobj *rules = snobj_list();

  for (int i = 0; i < num_fields_; i++) {
    struct snobj *f_obj = snobj_map();
    const struct EmField *f = &fields_[i];

    snobj_map_set(f_obj, "size", snobj_uint(f->size));
    snobj_map_set(f_obj, "mask", snobj_blob(&f->mask, f->size));

    if (f->attr_id < 0) {
      snobj_map_set(f_obj, "offset", snobj_uint(f->offset));
    } else {
      snobj_map_set(f_obj, "name",
                    snobj_str(ExactMatch::attrs[f->attr_id].name));
    }

    snobj_list_add(fields, f_obj);
  }

  uint32_t next = 0;
  void *key;

  while ((key = ht_.Iterate(&next))) {
    struct snobj *rule = snobj_list();

    for (int i = 0; i < num_fields_; i++) {
      const struct EmField *f = &fields_[i];

      snobj_list_add(rule,
                     snobj_blob(static_cast<uint8_t *>(key) + f->pos, f->size));
    }

    snobj_list_add(rules, rule);
  }

  snobj_map_set(r, "fields", fields);
  snobj_map_set(r, "rules", rules);

  return r;
}

struct snobj *ExactMatch::GatherKey(struct snobj *fields, em_hkey_t *key) {
  if (fields->size != static_cast<size_t>(num_fields_)) {
    return snobj_err(EINVAL, "must specify %d fields", num_fields_);
  }

  memset(key, 0, sizeof(*key));

  for (size_t i = 0; i < fields->size; i++) {
    int field_size = fields_[i].size;
    int field_pos = fields_[i].pos;

    struct snobj *f_obj = snobj_list_get(fields, i);
    uint64_t f;

    int force_be = (fields_[i].attr_id < 0);

    if (snobj_binvalue_get(f_obj, field_size, &f, force_be)) {
      return snobj_err(EINVAL, "idx %lu: not a correct %d-byte value", i,
                       field_size);
    }

    memcpy(reinterpret_cast<uint8_t *>(key) + field_pos, &f, field_size);
  }

  return nullptr;
}

pb_error_t ExactMatch::GatherKey(const RepeatedField<uint64_t> &fields,
                                 em_hkey_t *key) {
  if (fields.size() != num_fields_) {
    return pb_error(EINVAL, "must specify %d fields", num_fields_);
  }

  memset(key, 0, sizeof(*key));

  for (auto i = 0; i < fields.size(); i++) {
    int field_size = fields_[i].size;
    int field_pos = fields_[i].pos;

    uint64_t f;

    int force_be = (fields_[i].attr_id < 0);

    uint64_t f_obj = fields.Get(i);

    if (uint64_to_bin((uint8_t *)&f, field_size, f_obj,
                      bess::utils::is_be_system() | force_be)) {
      return pb_error(EINVAL, "idx %d: not a correct %d-byte value", i,
                      field_size);
    }

    memcpy(reinterpret_cast<uint8_t *>(key) + field_pos, &f, field_size);
  }

  return pb_errno(0);
}

struct snobj *ExactMatch::CommandAdd(struct snobj *arg) {
  struct snobj *fields = snobj_eval(arg, "fields");
  gate_idx_t gate = snobj_eval_uint(arg, "gate");

  em_hkey_t key;

  struct snobj *err;
  int ret;

  if (!snobj_eval_exists(arg, "gate")) {
    return snobj_err(EINVAL, "'gate' must be specified");
  }

  if (!is_valid_gate(gate)) {
    return snobj_err(EINVAL, "Invalid gate: %hu", gate);
  }

  if (!fields || snobj_type(fields) != TYPE_LIST) {
    return snobj_err(EINVAL, "'fields' must be a list");
  }

  if ((err = GatherKey(fields, &key))) {
    return err;
  }

  ret = ht_.Set(&key, &gate);
  if (ret) {
    return snobj_err(-ret, "ht_set() failed");
  }

  return nullptr;
}

pb_cmd_response_t ExactMatch::CommandAddPb(
    const bess::pb::ExactMatchCommandAddArg &arg) {
  em_hkey_t key;
  gate_idx_t gate = arg.gate();
  pb_error_t err;
  int ret;

  pb_cmd_response_t response;

  if (!is_valid_gate(gate)) {
    set_cmd_response_error(&response,
                           pb_error(EINVAL, "Invalid gate: %hu", gate));
    return response;
  }

  if (arg.fields_size() == 0) {
    set_cmd_response_error(&response,
                           pb_error(EINVAL, "'fields' must be a list"));
    return response;
  }

  if ((err = GatherKey(arg.fields(), &key)).err() != 0) {
    set_cmd_response_error(&response, err);
    return response;
  }

  ret = ht_.Set(&key, &gate);
  if (ret) {
    set_cmd_response_error(&response, pb_error(-ret, "ht_set() failed"));
    return response;
  }

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct snobj *ExactMatch::CommandDelete(struct snobj *arg) {
  em_hkey_t key;

  struct snobj *err;
  int ret;

  if (!arg || snobj_type(arg) != TYPE_LIST) {
    return snobj_err(EINVAL, "argument must be a list");
  }

  if ((err = GatherKey(arg, &key))) {
    return err;
  }

  ret = ht_.Del(&key);
  if (ret < 0) {
    return snobj_err(-ret, "ht_del() failed");
  }

  return nullptr;
}

pb_cmd_response_t ExactMatch::CommandDeletePb(
    const bess::pb::ExactMatchCommandDeleteArg &arg) {
  pb_cmd_response_t response;

  em_hkey_t key;

  pb_error_t err;
  int ret;

  if (arg.fields_size() == 0) {
    set_cmd_response_error(&response,
                           pb_error(EINVAL, "argument must be a list"));
    return response;
  }

  if ((err = GatherKey(arg.fields(), &key)).err() != 0) {
    set_cmd_response_error(&response, err);
    return response;
  }

  ret = ht_.Del(&key);
  if (ret < 0) {
    set_cmd_response_error(&response, pb_error(-ret, "ht_del() failed"));
    return response;
  }

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct snobj *ExactMatch::CommandClear(struct snobj *) {
  ht_.Clear();

  return nullptr;
}

pb_cmd_response_t ExactMatch::CommandClearPb(
    const bess::pb::EmptyArg &) {
  ht_.Clear();

  pb_cmd_response_t response;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct snobj *ExactMatch::CommandSetDefaultGate(struct snobj *arg) {
  int gate = snobj_int_get(arg);

  default_gate_ = gate;

  return nullptr;
}

pb_cmd_response_t ExactMatch::CommandSetDefaultGatePb(
    const bess::pb::ExactMatchCommandSetDefaultGateArg &arg) {
  pb_cmd_response_t response;
  default_gate_ = arg.gate();

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

ADD_MODULE(ExactMatch, "em", "Multi-field classifier with an exact match table")
