#include <rte_byteorder.h>

#include "../module.h"
#include "../time.h"
#include "../utils/random.h"

#define MAX_VARS 16

class RandomUpdate : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<RandomUpdate> cmds;

 private:
  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  int num_vars_ = {};
  struct var {
    uint32_t mask; /* bits with 1 won't be updated */
    uint32_t min;
    uint32_t range; /* == max - min + 1 */
    int16_t offset;
  } vars_[MAX_VARS] = {};

  Random rng_;
};

const Commands<RandomUpdate> RandomUpdate::cmds = {
    {"add", &RandomUpdate::CommandAdd, 0},
    {"clear", &RandomUpdate::CommandClear, 0},
};

struct snobj *RandomUpdate::CommandAdd(struct snobj *arg) {
  int curr = num_vars_;

  if (snobj_type(arg) != TYPE_LIST)
    return snobj_err(EINVAL, "argument must be a list of maps");

  if (curr + arg->size > MAX_VARS)
    return snobj_err(EINVAL,
                     "max %d variables "
                     "can be specified",
                     MAX_VARS);

  for (size_t i = 0; i < arg->size; i++) {
    struct snobj *var = snobj_list_get(arg, i);

    uint8_t size;
    int16_t offset;
    uint32_t mask;
    uint32_t min;
    uint32_t max;

    if (var->type != TYPE_MAP)
      return snobj_err(EINVAL, "argument must be a list of maps");

    offset = snobj_eval_int(var, "offset");
    size = snobj_eval_uint(var, "size");
    min = snobj_eval_uint(var, "min");
    max = snobj_eval_uint(var, "max");

    if (offset < 0) return snobj_err(EINVAL, "too small 'offset'");

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

    if (offset + 4 > SNBUF_DATA) return snobj_err(EINVAL, "too large 'offset'");

    if (min > max)
      return snobj_err(EINVAL,
                       "'min' should not be "
                       "greater than 'max'");

    vars_[curr + i].offset = offset;
    vars_[curr + i].mask = mask;
    vars_[curr + i].min = min;

    /* avoid modulo 0 */
    vars_[curr + i].range = (max - min + 1) ?: 0xffffffff;
  }

  num_vars_ = curr + arg->size;

  return NULL;
}

struct snobj *RandomUpdate::CommandClear(struct snobj *arg) {
  num_vars_ = 0;

  return NULL;
}

struct snobj *RandomUpdate::Init(struct snobj *arg) {
  struct snobj *t;

  if (!arg) return NULL;

  if (snobj_type(arg) != TYPE_MAP || !(t = snobj_eval(arg, "fields")))
    return snobj_err(EINVAL, "'fields' must be specified");

  return CommandAdd(t);
}

void RandomUpdate::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < num_vars_; i++) {
    const struct var *var = &vars_[i];

    uint32_t mask = var->mask;
    uint32_t min = var->min;
    uint32_t range = var->range;
    int16_t offset = var->offset;

    for (int j = 0; j < cnt; j++) {
      struct snbuf *snb = batch->pkts[j];
      char *head = static_cast<char *>(snb_head_data(snb));

      uint32_t *p  = (uint32_t *)(head + offset);
      uint32_t rand_val = min + rng_.GetRange(range);

      *p = (*p & mask) | rte_cpu_to_be_32(rand_val);
    }
  }

  run_next_module(this, batch);
}

ADD_MODULE(RandomUpdate, "rupdate", "updates packet data with random values")
