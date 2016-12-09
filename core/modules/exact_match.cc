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
     0},
    {"delete", "ExactMatchCommandDeleteArg",
     MODULE_CMD_FUNC(&ExactMatch::CommandDelete), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&ExactMatch::CommandClear), 0},
    {"set_default_gate", "ExactMatchCommandSetDefaultGateArg",
     MODULE_CMD_FUNC(&ExactMatch::CommandSetDefaultGate), 1}};

pb_error_t ExactMatch::AddFieldOne(const bess::pb::ExactMatchArg_Field &field,
                                   struct EmField *f, int idx) {
  f->size = field.size();
  if (f->size < 1 || f->size > MAX_FIELD_SIZE) {
    return pb_error(EINVAL, "idx %d: 'size' must be 1-%d", idx, MAX_FIELD_SIZE);
  }

  if (field.position_case() == bess::pb::ExactMatchArg_Field::kName) {
    const char *attr = field.name().c_str();
    f->attr_id = AddMetadataAttr(attr, f->size,
                                 bess::metadata::Attribute::AccessMode::kRead);
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

  if (field.mask() == 0) {
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

pb_error_t ExactMatch::Init(const bess::pb::ExactMatchArg &arg) {
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

void ExactMatch::DeInit() {
  ht_.Close();
}

void ExactMatch::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t default_gate;
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];

  int key_size = total_key_size_;
  char keys[bess::PacketBatch::kMaxBurst][HASH_KEY_SIZE] __ymm_aligned;

  int cnt = batch->cnt();

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
      offset = bess::Packet::mt_offset_to_databuf_offset(attr_offsets[attr_id]);
    }

    char *key = keys[0] + pos;

    for (int j = 0; j < cnt; j++, key += HASH_KEY_SIZE) {
      char *buf_addr = reinterpret_cast<char *>(batch->pkts()[j]->buffer());

      /* for offset-based attrs we use relative offset */
      if (attr_id < 0) {
        buf_addr += batch->pkts()[j]->data_off();
      }

      *(reinterpret_cast<uint64_t *>(key)) =
          *(reinterpret_cast<uint64_t *>(buf_addr + offset)) & mask;
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

pb_error_t ExactMatch::GatherKey(const RepeatedPtrField<std::string> &fields,
                                 em_hkey_t *key) {
  if (fields.size() != num_fields_) {
    return pb_error(EINVAL, "must specify %d fields", num_fields_);
  }

  memset(key, 0, sizeof(*key));

  for (auto i = 0; i < fields.size(); i++) {
    int field_size = fields_[i].size;
    int field_pos = fields_[i].pos;

    const std::string &f_obj = fields.Get(i);

    if (static_cast<size_t>(field_size) != f_obj.length()) {
      return pb_error(EINVAL, "idx %d: not a correct %d-byte value", i,
                      field_size);
    }

    memcpy(reinterpret_cast<uint8_t *>(key) + field_pos, f_obj.c_str(),
           field_size);
  }

  return pb_errno(0);
}

pb_cmd_response_t ExactMatch::CommandAdd(
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

pb_cmd_response_t ExactMatch::CommandDelete(
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

pb_cmd_response_t ExactMatch::CommandClear(const bess::pb::EmptyArg &) {
  ht_.Clear();

  pb_cmd_response_t response;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t ExactMatch::CommandSetDefaultGate(
    const bess::pb::ExactMatchCommandSetDefaultGateArg &arg) {
  pb_cmd_response_t response;
  default_gate_ = arg.gate();

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

ADD_MODULE(ExactMatch, "em", "Multi-field classifier with an exact match table")
