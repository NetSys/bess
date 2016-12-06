#include "random_update.h"

#include <rte_byteorder.h>

#include "../utils/time.h"

const Commands RandomUpdate::cmds = {
    {"add", "RandomUpdateArg", MODULE_CMD_FUNC(&RandomUpdate::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&RandomUpdate::CommandClear), 0},
};

pb_error_t RandomUpdate::Init(const bess::pb::RandomUpdateArg &arg) {
  pb_cmd_response_t response = CommandAdd(arg);
  return response.error();
}

pb_cmd_response_t RandomUpdate::CommandAdd(
    const bess::pb::RandomUpdateArg &arg) {
  pb_cmd_response_t response;

  int curr = num_vars_;
  if (curr + arg.fields_size() > MAX_VARS) {
    set_cmd_response_error(&response, pb_error(EINVAL,
                                               "max %d variables "
                                               "can be specified",
                                               MAX_VARS));
    return response;
  }

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &var = arg.fields(i);

    uint8_t size;
    int16_t offset;
    uint32_t mask;
    uint32_t min;
    uint32_t max;

    offset = var.offset();
    size = var.size();
    min = var.min();
    max = var.max();

    if (offset < 0) {
      set_cmd_response_error(&response, pb_error(EINVAL, "too small 'offset'"));
      return response;
    }

    switch (size) {
      case 1:
        offset -= 3;
        mask = rte_cpu_to_be_32(0xffffff00);
        min = std::min(min, static_cast<uint32_t>(0xff));
        max = std::min(max, static_cast<uint32_t>(0xff));
        break;

      case 2:
        offset -= 2;
        mask = rte_cpu_to_be_32(0xffff0000);
        min = std::min(min, static_cast<uint32_t>(0xffff));
        max = std::min(max, static_cast<uint32_t>(0xffff));
        break;

      case 4:
        mask = rte_cpu_to_be_32(0x00000000);
        min = std::min(min, static_cast<uint32_t>(0xffffffffu));
        max = std::min(max, static_cast<uint32_t>(0xffffffffu));
        break;

      default:
        set_cmd_response_error(&response,
                               pb_error(EINVAL, "'size' must be 1, 2, or 4"));
        return response;
    }

    if (offset + 4 > SNBUF_DATA) {
      set_cmd_response_error(&response, pb_error(EINVAL, "too large 'offset'"));
      return response;
    }

    if (min > max) {
      set_cmd_response_error(&response, pb_error(EINVAL,
                                                 "'min' should not be "
                                                 "greater than 'max'"));
      return response;
    }

    vars_[curr + i].offset = offset;
    vars_[curr + i].mask = mask;
    vars_[curr + i].min = min;

    /* avoid modulo 0 */
    vars_[curr + i].range = (max - min + 1) ?: 0xffffffff;
  }

  num_vars_ = curr + arg.fields_size();

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t RandomUpdate::CommandClear(const bess::pb::EmptyArg &) {
  num_vars_ = 0;

  pb_cmd_response_t response;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

void RandomUpdate::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < num_vars_; i++) {
    const auto var = &vars_[i];

    uint32_t mask = var->mask;
    uint32_t min = var->min;
    uint32_t range = var->range;
    int16_t offset = var->offset;

    for (int j = 0; j < cnt; j++) {
      bess::Packet *snb = batch->pkts()[j];
      char *head = snb->head_data<char *>();

      uint32_t *p = reinterpret_cast<uint32_t *>(head + offset);
      uint32_t rand_val = min + rng_.GetRange(range);

      *p = (*p & mask) | rte_cpu_to_be_32(rand_val);
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(RandomUpdate, "rupdate", "updates packet data with random values")
