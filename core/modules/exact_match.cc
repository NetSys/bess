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

  return pb_errno(0);
}

void ExactMatch::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t default_gate;
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];

  em_hkey_t keys[bess::PacketBatch::kMaxBurst] __ymm_aligned;

  int cnt = batch->cnt();

  // Initialize the padding with zero
  for (int i = 0; i < cnt; i++) {
    keys[i].u64_arr[(total_key_size_ - 1) / 8] = 0;
  }

  default_gate = ACCESS_ONCE(default_gate_);

  for (int i = 0; i < num_fields_; i++) {
    uint64_t mask = fields_[i].mask;
    int offset;
    int pos = fields_[i].pos;
    int attr_id = fields_[i].attr_id;

    if (attr_id < 0) {
      offset = fields_[i].offset;
    } else {
      offset = bess::Packet::mt_offset_to_databuf_offset(attr_offset(attr_id));
    }

    for (int j = 0; j < cnt; j++) {
      char *buf_addr = reinterpret_cast<char *>(batch->pkts()[j]->buffer());

      /* for offset-based attrs we use relative offset */
      if (attr_id < 0) {
        buf_addr += batch->pkts()[j]->data_off();
      }

      char *key = reinterpret_cast<char *>(keys[j].u64_arr) + pos;

      *(reinterpret_cast<uint64_t *>(key)) =
          *(reinterpret_cast<uint64_t *>(buf_addr + offset)) & mask;
    }
  }

  for (int i = 0; i < cnt; i++) {
    const auto &ht = ht_;
    const auto *entry =
        ht.Find(keys[i], em_hash(total_key_size_), em_eq(total_key_size_));
    out_gates[i] = entry ? entry->second : default_gate;
  }

  RunSplit(out_gates, batch);
}

std::string ExactMatch::GetDesc() const {
  return bess::utils::Format("%d fields, %zu rules", num_fields_, ht_.Count());
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

  ht_.Insert(key, gate, em_hash(total_key_size_), em_eq(total_key_size_));

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t ExactMatch::CommandDelete(
    const bess::pb::ExactMatchCommandDeleteArg &arg) {
  pb_cmd_response_t response;

  em_hkey_t key;

  pb_error_t err;

  if (arg.fields_size() == 0) {
    set_cmd_response_error(&response,
                           pb_error(EINVAL, "argument must be a list"));
    return response;
  }

  if ((err = GatherKey(arg.fields(), &key)).err() != 0) {
    set_cmd_response_error(&response, err);
    return response;
  }

  bool ret = ht_.Remove(key, em_hash(total_key_size_), em_eq(total_key_size_));
  if (!ret) {
    set_cmd_response_error(&response, pb_error(-1, "ht_del() failed"));
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
