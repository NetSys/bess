#include "random_update.h"

using bess::utils::be32_t;

const Commands RandomUpdate::cmds = {
    {"add", "RandomUpdateArg", MODULE_CMD_FUNC(&RandomUpdate::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&RandomUpdate::CommandClear), 0},
};

CommandResponse RandomUpdate::Init(const bess::pb::RandomUpdateArg &arg) {
  return CommandAdd(arg);
}

CommandResponse RandomUpdate::CommandAdd(const bess::pb::RandomUpdateArg &arg) {
  int curr = num_vars_;
  if (curr + arg.fields_size() > MAX_VARS) {
    return CommandFailure(EINVAL, "max %d variables can be specified",
                          MAX_VARS);
  }

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &var = arg.fields(i);

    size_t size;
    size_t offset;
    be32_t mask;
    uint32_t min;
    uint32_t max;

    offset = var.offset();
    size = var.size();
    min = var.min();
    max = var.max();

    switch (size) {
      case 1:
        mask = be32_t(0x00ffffff);
        min = std::min(min, static_cast<uint32_t>(0xff));
        max = std::min(max, static_cast<uint32_t>(0xff));
        break;

      case 2:
        mask = be32_t(0x0000ffff);
        min = std::min(min, static_cast<uint32_t>(0xffff));
        max = std::min(max, static_cast<uint32_t>(0xffff));
        break;

      case 4:
        mask = be32_t(0x00000000);
        min = std::min(min, static_cast<uint32_t>(0xffffffffu));
        max = std::min(max, static_cast<uint32_t>(0xffffffffu));
        break;

      default:
        return CommandFailure(EINVAL, "'size' must be 1, 2, or 4");
    }

    if (offset + size > SNBUF_DATA) {
      return CommandFailure(EINVAL, "too large 'offset'");
    }

    if (min > max) {
      return CommandFailure(EINVAL, "'min' should not be greater than 'max'");
    }

    vars_[curr + i].offset = offset;
    vars_[curr + i].mask = mask;
    vars_[curr + i].min = min;

    // avoid modulo 0
    vars_[curr + i].range = (max - min + 1) ?: 0xffffffff;
    vars_[curr + i].bit_shift = (4 - size) * 8;
  }

  num_vars_ = curr + arg.fields_size();
  return CommandSuccess();
}

CommandResponse RandomUpdate::CommandClear(const bess::pb::EmptyArg &) {
  num_vars_ = 0;
  return CommandSuccess();
}

void RandomUpdate::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < num_vars_; i++) {
    const auto var = &vars_[i];

    be32_t mask = var->mask;
    uint32_t min = var->min;
    uint32_t range = var->range;
    size_t offset = var->offset;
    size_t bit_shift = var->bit_shift;

    for (int j = 0; j < cnt; j++) {
      be32_t *p = batch->pkts()[j]->head_data<be32_t *>(offset);
      uint32_t rand_val = min + rng_.GetRange(range);
      *p = (*p & mask) | (be32_t(rand_val) << bit_shift);
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(RandomUpdate, "rupdate", "updates packet data with random values")
