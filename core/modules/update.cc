#include "update.h"

#include <rte_byteorder.h>

const Commands<Module> Update::cmds = {
    {"add", MODULE_FUNC &Update::CommandAdd, 0},
    {"clear", MODULE_FUNC &Update::CommandClear, 0},
};

const PbCommands Update::pb_cmds = {
    {"add", MODULE_CMD_FUNC(&Update::CommandAddPb), 0},
    {"clear", MODULE_CMD_FUNC(&Update::CommandClearPb), 0},
};

struct snobj *Update::Init(struct snobj *arg) {
  struct snobj *t;

  if (!arg) {
    return nullptr;
  }

  if (snobj_type(arg) != TYPE_MAP || !(t = snobj_eval(arg, "fields"))) {
    return snobj_err(EINVAL, "'fields' must be specified");
  }

  return CommandAdd(t);
}

pb_error_t Update::InitPb(const bess::pb::UpdateArg &arg) {
  pb_cmd_response_t response = CommandAddPb(arg);
  return response.error();
}

void Update::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < num_fields_; i++) {
    const auto field = &fields_[i];

    uint64_t mask = field->mask;
    uint64_t value = field->value;
    int16_t offset = field->offset;

    for (int j = 0; j < cnt; j++) {
      struct snbuf *snb = batch->pkts[j];
      char *head = static_cast<char *>(snb_head_data(snb));

      uint64_t *p = reinterpret_cast<uint64_t *>(head) + offset;

      *p = (*p & mask) | value;
    }
  }

  RunNextModule(batch);
}

pb_cmd_response_t Update::CommandAddPb(const bess::pb::UpdateArg &arg) {
  pb_cmd_response_t response;

  int curr = num_fields_;

  if (curr + arg.fields_size() > UPDATE_MAX_FIELDS) {
    set_cmd_response_error(&response, pb_error(EINVAL,
                                               "max %d variables "
                                               "can be specified",
                                               UPDATE_MAX_FIELDS));
    return response;
  }

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &field = arg.fields(i);

    uint8_t size;
    int16_t offset;
    uint64_t mask;
    uint64_t value;

    offset = field.offset();

    size = field.size();
    if (size < 1 || size > 8) {
      set_cmd_response_error(&response, pb_error(EINVAL, "'size' must be 1-8"));
      return response;
    }

    const char *t = field.value().c_str();
    memcpy(&value, t, size);

    if (offset < 0) {
      set_cmd_response_error(&response, pb_error(EINVAL, "too small 'offset'"));
      return response;
    }

    offset -= (8 - size);
    mask = ((uint64_t)1 << ((8 - size) * 8)) - 1;

    if (offset + 8 > SNBUF_DATA) {
      set_cmd_response_error(&response, pb_error(EINVAL, "too large 'offset'"));
      return response;
    }

    fields_[curr + i].offset = offset;
    fields_[curr + i].mask = mask;
    fields_[curr + i].value = rte_cpu_to_be_64(value);
  }

  num_fields_ = curr + arg.fields_size();
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t Update::CommandClearPb(const bess::pb::EmptyArg &) {
  num_fields_ = 0;

  pb_cmd_response_t response;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct snobj *Update::CommandAdd(struct snobj *arg) {
  int curr = num_fields_;

  if (snobj_type(arg) != TYPE_LIST) {
    return snobj_err(EINVAL, "argument must be a list of maps");
  }

  if (curr + arg->size > UPDATE_MAX_FIELDS) {
    return snobj_err(EINVAL,
                     "max %d variables "
                     "can be specified",
                     UPDATE_MAX_FIELDS);
  }

  for (size_t i = 0; i < arg->size; i++) {
    struct snobj *field = snobj_list_get(arg, i);

    uint8_t size;
    int16_t offset;
    uint64_t mask;
    uint64_t value;

    struct snobj *t;

    if (field->type != TYPE_MAP) {
      return snobj_err(EINVAL, "argument must be a list of maps");
    }

    offset = snobj_eval_int(field, "offset");

    size = snobj_eval_uint(field, "size");
    if (size < 1 || size > 8) {
      return snobj_err(EINVAL, "'size' must be 1-8");
    }

    t = snobj_eval(field, "value");
    if (snobj_binvalue_get(t, size, &value, 0)) {
      return snobj_err(EINVAL,
                       "'value' field has not a "
                       "correct %d-byte value",
                       size);
    }

    if (offset < 0) {
      return snobj_err(EINVAL, "too small 'offset'");
    }

    offset -= (8 - size);
    mask = ((uint64_t)1 << ((8 - size) * 8)) - 1;

    if (offset + 8 > SNBUF_DATA) {
      return snobj_err(EINVAL, "too large 'offset'");
    }

    fields_[curr + i].offset = offset;
    fields_[curr + i].mask = mask;
    fields_[curr + i].value = rte_cpu_to_be_64(value);
  }

  num_fields_ = curr + arg->size;

  return nullptr;
}

struct snobj *Update::CommandClear(struct snobj *) {
  num_fields_ = 0;

  return nullptr;
}

ADD_MODULE(Update, "update", "updates packet data with specified values")
