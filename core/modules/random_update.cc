#include "random_update.h"

#include <rte_byteorder.h>

#include "../utils/time.h"

const Commands<Module> RandomUpdate::cmds = {
    {"add", MODULE_FUNC &RandomUpdate::CommandAdd, 0},
    {"clear", MODULE_FUNC &RandomUpdate::CommandClear, 0},
};

const PbCommands RandomUpdate::pb_cmds = {
    {"add", MODULE_CMD_FUNC(&RandomUpdate::CommandAddPb), 0},
    {"clear", MODULE_CMD_FUNC(&RandomUpdate::CommandClearPb), 0},
};

struct snobj *RandomUpdate::CommandAdd(struct snobj *arg) {
  int curr = num_vars_;

  if (snobj_type(arg) != TYPE_LIST) {
    return snobj_err(EINVAL, "argument must be a list of maps");
  }

  if (curr + arg->size > MAX_VARS) {
    return snobj_err(EINVAL,
                     "max %d variables "
                     "can be specified",
                     MAX_VARS);
  }

  for (size_t i = 0; i < arg->size; i++) {
    struct snobj *var = snobj_list_get(arg, i);

    uint8_t size;
    int16_t offset;
    uint32_t mask;
    uint32_t min;
    uint32_t max;

    if (var->type != TYPE_MAP) {
      return snobj_err(EINVAL, "argument must be a list of maps");
    }

    offset = snobj_eval_int(var, "offset");
    size = snobj_eval_uint(var, "size");
    min = snobj_eval_uint(var, "min");
    max = snobj_eval_uint(var, "max");

    if (offset < 0) {
      return snobj_err(EINVAL, "too small 'offset'");
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
        return snobj_err(EINVAL, "'size' must be 1, 2, or 4");
    }

    if (offset + 4 > SNBUF_DATA) {
      return snobj_err(EINVAL, "too large 'offset'");
    }

    if (min > max) {
      return snobj_err(EINVAL,
                       "'min' should not be "
                       "greater than 'max'");
    }

    vars_[curr + i].offset = offset;
    vars_[curr + i].mask = mask;
    vars_[curr + i].min = min;

    /* avoid modulo 0 */
    vars_[curr + i].range = (max - min + 1) ?: 0xffffffff;
  }

  num_vars_ = curr + arg->size;

  return nullptr;
}

struct snobj *RandomUpdate::CommandClear(struct snobj *) {
  num_vars_ = 0;

  return nullptr;
}

struct snobj *RandomUpdate::Init(struct snobj *arg) {
  struct snobj *t;

  if (!arg) {
    return nullptr;
  }

  if (snobj_type(arg) != TYPE_MAP || !(t = snobj_eval(arg, "fields"))) {
    return snobj_err(EINVAL, "'fields' must be specified");
  }

  return CommandAdd(t);
}

pb_error_t RandomUpdate::InitPb(const bess::pb::RandomUpdateArg &arg) {
  pb_cmd_response_t response = CommandAddPb(arg);
  return response.error();
}

pb_cmd_response_t RandomUpdate::CommandAddPb(
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

pb_cmd_response_t RandomUpdate::CommandClearPb(
    const bess::pb::EmptyArg &) {
  num_vars_ = 0;

  pb_cmd_response_t response;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

void RandomUpdate::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < num_vars_; i++) {
    const auto var = &vars_[i];

    uint32_t mask = var->mask;
    uint32_t min = var->min;
    uint32_t range = var->range;
    int16_t offset = var->offset;

    for (int j = 0; j < cnt; j++) {
      struct snbuf *snb = batch->pkts[j];
      char *head = static_cast<char *>(snb_head_data(snb));

      uint32_t *p = (uint32_t *)(head + offset);
      uint32_t rand_val = min + rng_.GetRange(range);

      *p = (*p & mask) | rte_cpu_to_be_32(rand_val);
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(RandomUpdate, "rupdate", "updates packet data with random values")
