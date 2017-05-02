#include "update.h"
#include "../utils/endian.h"
#include <rte_byteorder.h>

const Commands Update::cmds = {
    {"add", "UpdateArg", MODULE_CMD_FUNC(&Update::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&Update::CommandClear), 0},
};

CommandResponse Update::Init(const bess::pb::UpdateArg &arg) {
  return CommandAdd(arg);
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

CommandResponse Update::CommandAdd(const bess::pb::UpdateArg &arg) {
  int curr = num_fields_;

  if (curr + arg.fields_size() > UPDATE_MAX_FIELDS) {
    return CommandFailure(EINVAL, "max %d variables can be specified",
                          UPDATE_MAX_FIELDS);
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
      return CommandFailure(EINVAL, "'size' must be 1-8");
    }

    if (!bess::utils::uint64_to_bin(&value, field.value(), size,
                                    bess::utils::is_be_system())) {
      return CommandFailure(
          EINVAL, "'value' field has not a correct %d-byte value", size);
    }

    if (offset < 0) {
      return CommandFailure(EINVAL, "too small 'offset'");
    }

    offset -= (8 - size);
    mask = (1ull << ((8 - size) * 8)) - 1;

    if (offset + 8 > SNBUF_DATA) {
      return CommandFailure(EINVAL, "too large 'offset'");
    }

    fields_[curr + i].offset = offset;
    fields_[curr + i].mask = mask;
    fields_[curr + i].value = rte_cpu_to_be_64(value);
  }

  num_fields_ = curr + arg.fields_size();
  return CommandSuccess();
}

CommandResponse Update::CommandClear(const bess::pb::EmptyArg &) {
  num_fields_ = 0;
  return CommandSuccess();
}

ADD_MODULE(Update, "update", "updates packet data with specified values")
