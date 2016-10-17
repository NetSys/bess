#include "../utils/htable.h"

#include "../module.h"

#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8
static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define HASH_KEY_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

typedef struct { uint64_t u64_arr[MAX_FIELDS]; } hkey_t;

HT_DECLARE_INLINED_FUNCS(em, hkey_t)

static inline int em_keycmp(const hkey_t *key, const hkey_t *key_stored,
                            size_t key_len) {
  const uint64_t *a = key->u64_arr;
  const uint64_t *b = key_stored->u64_arr;

  switch (key_len >> 3) {
    default:
      promise_unreachable();
    case 8:
      if (unlikely(a[7] != b[7])) return 1;
    case 7:
      if (unlikely(a[6] != b[6])) return 1;
    case 6:
      if (unlikely(a[5] != b[5])) return 1;
    case 5:
      if (unlikely(a[4] != b[4])) return 1;
    case 4:
      if (unlikely(a[3] != b[3])) return 1;
    case 3:
      if (unlikely(a[2] != b[2])) return 1;
    case 2:
      if (unlikely(a[1] != b[1])) return 1;
    case 1:
      if (unlikely(a[0] != b[0])) return 1;
  }

  return 0;
}

static inline uint32_t em_hash(const hkey_t *key, uint32_t key_len,
                               uint32_t init_val) {
#if __SSE4_2__ && __x86_64
  const uint64_t *a = key->u64_arr;

  switch (key_len >> 3) {
    default:
      promise_unreachable();
    case 8:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 7:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 6:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 5:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 4:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 3:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 2:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 1:
      init_val = crc32c_sse42_u64(*a++, init_val);
  }

  return init_val;
#else
  return rte_hash_crc(key, key_len, init_val);
#endif
}

// XXX: this is repeated in many modules. get rid of them when converting .h to
// .hh, etc... it's in defined in some old header
static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

struct EmField {
  /* bits with 1: the bit must be considered.
   * bits with 0: don't care */
  uint64_t mask;

  int attr_id; /* -1 for offset-based fields */

  /* Relative offset in the packet data for offset-based fields.
   *  (starts from data_off, not the beginning of the headroom */
  int offset;

  int pos; /* relative position in the key */

  int size; /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

class ExactMatch : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual struct snobj *GetDesc();
  virtual struct snobj *GetDump();

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const std::vector<struct Command> cmds;

 private:
  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandDelete(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);
  struct snobj *CommandSetDefaultGate(struct snobj *arg);

  struct snobj *AddFieldOne(struct snobj *field, struct EmField *f, int idx);
  struct snobj *GatherKey(struct snobj *fields, hkey_t *key);

  gate_idx_t default_gate_;

  uint32_t total_key_size_;

  int num_fields_;
  struct EmField fields_[MAX_FIELDS];

  struct htable ht_;
};

const std::vector<struct Command> ExactMatch::cmds = {
    {"add", static_cast<CmdFunc>(&ExactMatch::CommandAdd), 0},
    {"delete", static_cast<CmdFunc>(&ExactMatch::CommandDelete), 0},
    {"clear", static_cast<CmdFunc>(&ExactMatch::CommandClear), 0},
    {"set_default_gate",
     static_cast<CmdFunc>(&ExactMatch::CommandSetDefaultGate), 1}};

struct snobj *ExactMatch::AddFieldOne(struct snobj *field, struct EmField *f,
                                      int idx) {
  if (field->type != TYPE_MAP)
    return snobj_err(EINVAL, "'fields' must be a list of maps");

  f->size = snobj_eval_uint(field, "size");
  if (f->size < 1 || f->size > MAX_FIELD_SIZE)
    return snobj_err(EINVAL, "idx %d: 'size' must be 1-%d", idx,
                     MAX_FIELD_SIZE);

  const char *attr = static_cast<char *>(snobj_eval_str(field, "attr"));

  if (attr) {
    f->attr_id = add_metadata_attr(this, attr, f->size, MT_READ);
    if (f->attr_id < 0)
      return snobj_err(-f->attr_id, "idx %d: add_metadata_attr() failed", idx);
  } else if (snobj_eval_exists(field, "offset")) {
    f->attr_id = -1;
    f->offset = snobj_eval_int(field, "offset");
    if (f->offset < 0 || f->offset > 1024)
      return snobj_err(EINVAL, "idx %d: invalid 'offset'", idx);
  } else
    return snobj_err(EINVAL, "idx %d: must specify 'offset' or 'attr'", idx);

  struct snobj *mask = snobj_eval(field, "mask");
  int force_be = (f->attr_id < 0);

  if (!mask) {
    /* by default all bits are considered */
    f->mask = ((uint64_t)1 << (f->size * 8)) - 1;
  } else if (snobj_binvalue_get(mask, f->size, &f->mask, force_be))
    return snobj_err(EINVAL, "idx %d: not a correct %d-byte mask", idx,
                     f->size);

  if (f->mask == 0) return snobj_err(EINVAL, "idx %d: empty mask", idx);

  return NULL;
}

/* Takes a list of fields. Each field needs 'offset' (or 'name') and 'size',
 * and optional "mask" (0xfffff.. by default)
 *
 * e.g.: ExactMatch([{'offset': 14, 'size': 1, 'mask':0xf0}, ...]
 * (checks the IP version field)
 *
 * You can also specify metadata attributes
 * e.g.: ExactMatch([{'name': 'nexthop', 'size': 4}, ...] */
struct snobj *ExactMatch::Init(struct snobj *arg) {
  int size_acc = 0;

  struct snobj *fields = snobj_eval(arg, "fields");

  if (snobj_type(fields) != TYPE_LIST)
    return snobj_err(EINVAL, "'fields' must be a list of maps");

  for (size_t i = 0; i < fields->size; i++) {
    struct snobj *field = snobj_list_get(fields, i);
    struct snobj *err;
    struct EmField *f = &this->fields_[i];

    f->pos = size_acc;

    err = this->AddFieldOne(field, f, i);
    if (err) return err;

    size_acc += f->size;
  }

  this->default_gate_ = DROP_GATE;
  this->num_fields_ = fields->size;
  this->total_key_size_ = align_ceil(size_acc, sizeof(uint64_t));

  int ret = ht_init(&ht_, this->total_key_size_, sizeof(gate_idx_t));
  if (ret < 0) return snobj_err(-ret, "hash table creation failed");

  return NULL;
}

void ExactMatch::Deinit() { ht_close(&this->ht_); }

void ExactMatch::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t default_gate;
  gate_idx_t ogates[MAX_PKT_BURST];

  int key_size = this->total_key_size_;
  char keys[MAX_PKT_BURST][HASH_KEY_SIZE] __ymm_aligned;

  int cnt = batch->cnt;

  default_gate = ACCESS_ONCE(this->default_gate_);

  for (int i = 0; i < cnt; i++)
    memset(&keys[i][key_size - 8], 0, sizeof(uint64_t));

  for (int i = 0; i < this->num_fields_; i++) {
    uint64_t mask = this->fields_[i].mask;
    int offset;
    int pos = this->fields_[i].pos;
    int attr_id = this->fields_[i].attr_id;

    if (attr_id < 0)
      offset = this->fields_[i].offset;
    else
      offset = mt_offset_to_databuf_offset(this->attr_offsets[attr_id]);

    char *key = keys[0] + pos;

    for (int j = 0; j < cnt; j++, key += HASH_KEY_SIZE) {
      char *buf_addr = (char *)batch->pkts[j]->mbuf.buf_addr;

      /* for offset-based attrs we use relative offset */
      if (attr_id < 0) buf_addr += batch->pkts[j]->mbuf.data_off;

      *(uint64_t *)key = *(uint64_t *)(buf_addr + offset) & mask;
    }
  }

  const struct htable *t = &ht_;

  for (int i = 0; i < cnt; i++) {
    gate_idx_t *ret = static_cast<gate_idx_t *>(ht_em_get(t, keys[i]));
    ogates[i] = ret ? *ret : default_gate;
  }

  run_split(this, ogates, batch);
}

struct snobj *ExactMatch::GetDesc() {
  return snobj_str_fmt("%d fields, %d rules", this->num_fields_, this->ht_.cnt);
}

struct snobj *ExactMatch::GetDump() {
  struct snobj *r = snobj_map();
  struct snobj *fields = snobj_list();
  struct snobj *rules = snobj_list();

  for (int i = 0; i < this->num_fields_; i++) {
    struct snobj *f_obj = snobj_map();
    const struct EmField *f = &this->fields_[i];

    snobj_map_set(f_obj, "size", snobj_uint(f->size));
    snobj_map_set(f_obj, "mask", snobj_blob(&f->mask, f->size));

    if (f->attr_id < 0)
      snobj_map_set(f_obj, "offset", snobj_uint(f->offset));
    else
      snobj_map_set(f_obj, "name",
                    snobj_str(ExactMatch::attrs[f->attr_id].name));

    snobj_list_add(fields, f_obj);
  }

  uint32_t next = 0;
  void *key;

  while ((key = ht_iterate(&this->ht_, &next))) {
    struct snobj *rule = snobj_list();

    for (int i = 0; i < this->num_fields_; i++) {
      const struct EmField *f = &this->fields_[i];

      snobj_list_add(rule,
                     snobj_blob(static_cast<uint8_t *>(key) + f->pos, f->size));
    }

    snobj_list_add(rules, rule);
  }

  snobj_map_set(r, "fields", fields);
  snobj_map_set(r, "rules", rules);

  return r;
}

struct snobj *ExactMatch::GatherKey(struct snobj *fields, hkey_t *key) {
  if (fields->size != static_cast<size_t>(this->num_fields_))
    return snobj_err(EINVAL, "must specify %d fields", this->num_fields_);

  memset(key, 0, sizeof(*key));

  for (size_t i = 0; i < fields->size; i++) {
    int field_size = this->fields_[i].size;
    int field_pos = this->fields_[i].pos;

    struct snobj *f_obj = snobj_list_get(fields, i);
    uint64_t f;

    int force_be = (this->fields_[i].attr_id < 0);

    if (snobj_binvalue_get(f_obj, field_size, &f, force_be))
      return snobj_err(EINVAL, "idx %lu: not a correct %d-byte value", i,
                       field_size);

    memcpy(reinterpret_cast<uint8_t *>(key) + field_pos, &f, field_size);
  }

  return NULL;
}

struct snobj *ExactMatch::CommandAdd(struct snobj *arg) {
  struct snobj *fields = snobj_eval(arg, "fields");
  gate_idx_t gate = snobj_eval_uint(arg, "gate");

  hkey_t key;

  struct snobj *err;
  int ret;

  if (!snobj_eval_exists(arg, "gate"))
    return snobj_err(EINVAL, "'gate' must be specified");

  if (!is_valid_gate(gate)) return snobj_err(EINVAL, "Invalid gate: %hu", gate);

  if (!fields || snobj_type(fields) != TYPE_LIST)
    return snobj_err(EINVAL, "'fields' must be a list");

  if ((err = this->GatherKey(fields, &key))) return err;

  ret = ht_set(&this->ht_, &key, &gate);
  if (ret) return snobj_err(-ret, "ht_set() failed");

  return NULL;
}

struct snobj *ExactMatch::CommandDelete(struct snobj *arg) {
  hkey_t key;

  struct snobj *err;
  int ret;

  if (!arg || snobj_type(arg) != TYPE_LIST)
    return snobj_err(EINVAL, "argument must be a list");

  if ((err = this->GatherKey(arg, &key))) return err;

  ret = ht_del(&this->ht_, &key);
  if (ret < 0) return snobj_err(-ret, "ht_del() failed");

  return NULL;
}

struct snobj *ExactMatch::CommandClear(struct snobj *arg) {
  ht_clear(&this->ht_);

  return NULL;
}

struct snobj *ExactMatch::CommandSetDefaultGate(struct snobj *arg) {
  int gate = snobj_int_get(arg);

  this->default_gate_ = gate;

  return NULL;
}

ADD_MODULE(ExactMatch, "em", "Multi-field classifier with an exact match table")
