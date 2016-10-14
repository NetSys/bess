#include <rte_byteorder.h>

#include "../module.h"

#define MAX_FIELDS 16

class Update : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const std::vector<struct Command> cmds;

 private:
  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  int num_fields_ = {0};

  struct field {
    uint64_t mask;  /* bits with 1 won't be updated */
    uint64_t value; /* in network order */
    int16_t offset;
  } fields_[MAX_FIELDS] = {{0}};
};

const std::vector<struct Command> Update::cmds = {
    {"add", static_cast<CmdFunc>(&Update::CommandAdd), 0},
    {"clear", static_cast<CmdFunc>(&Update::CommandClear), 0},
};

struct snobj *Update::Init(struct snobj *arg) {
  struct snobj *t;

  if (!arg) return NULL;

  if (snobj_type(arg) != TYPE_MAP || !(t = snobj_eval(arg, "fields")))
    return snobj_err(EINVAL, "'fields' must be specified");

  return this->CommandAdd(t);
}

void Update::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < this->num_fields_; i++) {
    const struct field *field = &this->fields_[i];

    uint64_t mask = field->mask;
    uint64_t value = field->value;
    int16_t offset = field->offset;

    for (int j = 0; j < cnt; j++) {
      struct snbuf *snb = batch->pkts[j];
      char *head = static_cast<char *>(snb_head_data(snb));

      uint64_t *restrict p = reinterpret_cast<uint64_t *>(head) + offset;

      *p = (*p & mask) | value;
    }
  }

  run_next_module(this, batch);
}

struct snobj *Update::CommandAdd(struct snobj *arg) {
  int curr = this->num_fields_;

  if (snobj_type(arg) != TYPE_LIST)
    return snobj_err(EINVAL, "argument must be a list of maps");

  if (curr + arg->size > MAX_FIELDS)
    return snobj_err(EINVAL,
                     "max %d variables "
                     "can be specified",
                     MAX_FIELDS);

  for (size_t i = 0; i < arg->size; i++) {
    struct snobj *field = snobj_list_get(arg, i);

    uint8_t size;
    int16_t offset;
    uint64_t mask;
    uint64_t value;

    struct snobj *t;

    if (field->type != TYPE_MAP)
      return snobj_err(EINVAL, "argument must be a list of maps");

    offset = snobj_eval_int(field, "offset");

    size = snobj_eval_uint(field, "size");
    if (size < 1 || size > 8) return snobj_err(EINVAL, "'size' must be 1-8");

    t = snobj_eval(field, "value");
    if (snobj_binvalue_get(t, size, &value, 0)) {
      return snobj_err(EINVAL,
                       "'value' field has not a "
                       "correct %d-byte value",
                       size);
    }

    if (offset < 0) return snobj_err(EINVAL, "too small 'offset'");

    offset -= (8 - size);
    mask = ((uint64_t)1 << ((8 - size) * 8)) - 1;

    if (offset + 8 > SNBUF_DATA) return snobj_err(EINVAL, "too large 'offset'");

    this->fields_[curr + i].offset = offset;
    this->fields_[curr + i].mask = mask;
    this->fields_[curr + i].value = rte_cpu_to_be_64(value);
  }

  this->num_fields_ = curr + arg->size;

  return NULL;
}

struct snobj *Update::CommandClear(struct snobj *arg) {
  this->num_fields_ = 0;

  return NULL;
}

ModuleClassRegister<Update> update("Update", "update",
                                   "updates packet data with specified values");
