#include "update.h"

#include "../utils/endian.h"

const Commands Update::cmds = {
    {"add", "UpdateArg", MODULE_CMD_FUNC(&Update::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&Update::CommandClear), 0},
};

CommandResponse Update::Init(const bess::pb::UpdateArg &arg) {
  return CommandAdd(arg);
}

void Update::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (size_t i = 0; i < num_fields_; i++) {
    const auto field = &fields_[i];

    be64_t mask = field->mask;
    be64_t value = field->value;
    int16_t offset = field->offset;  // could be < 0

    for (int j = 0; j < cnt; j++) {
      bess::Packet *snb = batch->pkts()[j];
      char *head = snb->head_data<char *>();

      be64_t *p = reinterpret_cast<be64_t *>(head + offset);
      *p = (*p & mask) | value;
    }
  }

  RunNextModule(batch);
}

CommandResponse Update::CommandAdd(const bess::pb::UpdateArg &arg) {
  size_t curr = num_fields_;

  if (curr + arg.fields_size() > kMaxFields) {
    return CommandFailure(EINVAL, "max %zu variables can be specified",
                          kMaxFields);
  }

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &field = arg.fields(i);

    size_t size = field.size();
    if (size < 1 || size > 8) {
      return CommandFailure(EINVAL, "'size' must be 1-8");
    }

    if (field.offset() + size > SNBUF_DATA) {
      return CommandFailure(EINVAL, "too large 'offset'");
    }

    be64_t value(field.value() << ((8 - size) * 8));
    be64_t mask((1ull << ((8 - size) * 8)) - 1);

    if ((value & mask).value() != 0) {
      LOG(INFO) << value << ' ' << mask;
      return CommandFailure(
          EINVAL, "'value' field has not a correct %zu-byte value", size);
    }

    fields_[curr + i].offset = field.offset();
    fields_[curr + i].mask = mask;
    fields_[curr + i].value = value;
  }

  num_fields_ = curr + arg.fields_size();
  return CommandSuccess();
}

CommandResponse Update::CommandClear(const bess::pb::EmptyArg &) {
  num_fields_ = 0;
  return CommandSuccess();
}

ADD_MODULE(Update, "update", "updates packet data with specified values")
