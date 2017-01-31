#include "update.h"
#include "../utils/endian.h"
#include <rte_byteorder.h>

const Commands Update::cmds = {
    {"add", "UpdateArg", MODULE_CMD_FUNC(&Update::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&Update::CommandClear), 0},
};

pb_error_t Update::Init(const bess::pb::UpdateArg &arg) {
  pb_cmd_response_t response = CommandAdd(arg);
  return response.error();
}

void Update::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < num_fields_; i++) {
    const auto field = &fields_[i];

    uint64_t mask = field->mask;
    uint64_t value = field->value;
    int16_t offset = field->offset;  // could be < 0

    for (int j = 0; j < cnt; j++) {
      bess::Packet *snb = batch->pkts()[j];
      char *head = snb->head_data<char *>();

      uint64_t *p = reinterpret_cast<uint64_t *>(head + offset);
      *p = (*p & mask) | value;
    }
  }

  RunNextModule(batch);
}

pb_cmd_response_t Update::CommandAdd(const bess::pb::UpdateArg &arg) {
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

    if (uint64_to_bin((uint8_t *)&value, size, field.value(),
                      bess::utils::is_be_system())) {
      set_cmd_response_error(&response, pb_error(EINVAL,
                                                 "'value' field has not a "
                                                 "correct %d-byte value",
                                                 size));
      return response;
    }

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

pb_cmd_response_t Update::CommandClear(const bess::pb::EmptyArg &) {
  num_fields_ = 0;

  pb_cmd_response_t response;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

ADD_MODULE(Update, "update", "updates packet data with specified values")
