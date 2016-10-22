#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../utils/htable.h"
#include "../module.h"

#define MAX_TUPLES 8
#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8
static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define HASH_KEY_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

typedef struct { uint64_t u64_arr[MAX_FIELDS]; } hkey_t;

struct WmData {
  int priority;
  gate_idx_t ogate;
};

struct WmField {
  int attr_id; /* -1 for offset-based fields */

  /* Relative offset in the packet data for offset-based fields.
   *  (starts from data_off, not the beginning of the headroom */
  int offset;

  int pos; /* relative position in the key */

  int size; /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

static inline int wm_keycmp(const void *key, const void *key_stored,
                            size_t key_len) {
  const uint64_t *a = ((hkey_t *)key)->u64_arr;
  const uint64_t *b = ((hkey_t *)key_stored)->u64_arr;

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

static inline uint32_t wm_hash(const void *key, uint32_t key_len,
                               uint32_t init_val) {
#if __SSE4_2__ && __x86_64
  const uint64_t *a = ((hkey_t *)key)->u64_arr;

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

struct WmTuple {
  HTable<hkey_t, struct WmData, wm_keycmp, wm_hash> ht;
  hkey_t mask;
};

/* k1 = k2 & mask */
static void mask(void *k1, const void *k2, const void *mask, int key_size) {
  uint64_t *a = static_cast<uint64_t *>(k1);
  const uint64_t *b = reinterpret_cast<const uint64_t *>(k2);
  const uint64_t *m = reinterpret_cast<const uint64_t *>(mask);

  switch (key_size >> 3) {
    default:
      promise_unreachable();
    case 8:
      a[7] = b[7] & m[7];
    case 7:
      a[6] = b[6] & m[6];
    case 6:
      a[5] = b[5] & m[5];
    case 5:
      a[4] = b[4] & m[4];
    case 4:
      a[3] = b[3] & m[3];
    case 3:
      a[2] = b[2] & m[2];
    case 2:
      a[1] = b[1] & m[1];
    case 1:
      a[0] = b[0] & m[0];
  }
}

// XXX: this is repeated in many modules. get rid of them when converting .h to
// .hh, etc... it's in defined in some old header
static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

class WildcardMatch : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual struct snobj *GetDesc();
  virtual struct snobj *GetDump();

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<WildcardMatch> cmds;

 private:
  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandDelete(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);
  struct snobj *CommandSetDefaultGate(struct snobj *arg);

  gate_idx_t LookupEntry(hkey_t *key, gate_idx_t def_gate);
  struct snobj *AddFieldOne(struct snobj *field, struct WmField *f);

  void CollectRules(const struct WmTuple *tuple, struct snobj *rules);
  struct snobj *ExtractKeyMask(struct snobj *arg, hkey_t *key, hkey_t *mask);
  int FindTuple(hkey_t *mask);
  int AddTuple(hkey_t *mask);
  int AddEntry(struct WmTuple *tuple, hkey_t *key, struct WmData *data);
  int DelEntry(struct WmTuple *tuple, hkey_t *key);

  gate_idx_t default_gate_;

  int total_key_size_; /* a multiple of sizeof(uint64_t) */

  size_t num_fields_;
  struct WmField fields_[MAX_FIELDS];

  int num_tuples_;
  struct WmTuple tuples_[MAX_TUPLES];

  int next_table_id_;
};

const Commands<WildcardMatch> WildcardMatch::cmds = {
    {"add", &WildcardMatch::CommandAdd, 0},
    {"delete", &WildcardMatch::CommandDelete, 0},
    {"clear", &WildcardMatch::CommandClear, 0},
    {"set_default_gate", &WildcardMatch::CommandSetDefaultGate, 1}};

struct snobj *WildcardMatch::AddFieldOne(struct snobj *field,
                                         struct WmField *f) {
  if (field->type != TYPE_MAP)
    return snobj_err(EINVAL, "'fields' must be a list of maps");

  f->size = snobj_eval_uint(field, "size");

  if (f->size < 1 || f->size > MAX_FIELD_SIZE)
    return snobj_err(EINVAL, "'size' must be 1-%d", MAX_FIELD_SIZE);

  if (snobj_eval_exists(field, "offset")) {
    f->attr_id = -1;
    f->offset = snobj_eval_int(field, "offset");
    if (f->offset < 0 || f->offset > 1024)
      return snobj_err(EINVAL, "too small 'offset'");
    return NULL;
  }

  const char *attr = snobj_eval_str(field, "attr");
  if (!attr) return snobj_err(EINVAL, "specify 'offset' or 'attr'");

  f->attr_id = add_metadata_attr(this, attr, f->size, MT_READ);
  if (f->attr_id < 0)
    return snobj_err(-f->attr_id, "add_metadata_attr() failed");

  return NULL;
}

/* Takes a list of all fields that may be used by rules.
 * Each field needs 'offset' (or 'name') and 'size' in bytes,
 *
 * e.g.: WildcardMatch([{'offset': 26, 'size': 4}, ...]
 * (checks the source IP address)
 *
 * You can also specify metadata attributes
 * e.g.: WildcardMatch([{'name': 'nexthop', 'size': 4}, ...] */
struct snobj *WildcardMatch::Init(struct snobj *arg) {
  int size_acc = 0;

  struct snobj *fields = snobj_eval(arg, "fields");

  if (snobj_type(fields) != TYPE_LIST)
    return snobj_err(EINVAL, "'fields' must be a list of maps");

  for (size_t i = 0; i < fields->size; i++) {
    struct snobj *field = snobj_list_get(fields, i);
    struct snobj *err;
    struct WmField f;

    f.pos = size_acc;

    err = AddFieldOne(field, &f);
    if (err) return err;

    size_acc += f.size;
    fields_[i] = f;
  }

  default_gate_ = DROP_GATE;
  num_fields_ = fields->size;
  total_key_size_ = align_ceil(size_acc, sizeof(uint64_t));

  return NULL;
}

void WildcardMatch::Deinit() {
  for (int i = 0; i < num_tuples_; i++) tuples_[i].ht.Close();
}

gate_idx_t WildcardMatch::LookupEntry(hkey_t *key, gate_idx_t def_gate) {
  struct WmData result = {
      .priority = INT_MIN, .ogate = def_gate,
  };

  const int key_size = total_key_size_;
  const int num_tuples = num_tuples_;

  hkey_t key_masked;

  for (int i = 0; i < num_tuples; i++) {
    struct WmTuple *tuple = &tuples_[i];
    struct WmData *cand;

    mask(&key_masked, key, &tuple->mask, key_size);

    cand = static_cast<struct WmData *>(tuple->ht.Get(&key_masked));

    if (cand && cand->priority >= result.priority) result = *cand;
  }

  return result.ogate;
}

void WildcardMatch::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t default_gate;
  gate_idx_t ogates[MAX_PKT_BURST];

  char keys[MAX_PKT_BURST][HASH_KEY_SIZE] __ymm_aligned;

  int cnt = batch->cnt;

  default_gate = ACCESS_ONCE(default_gate_);

  for (size_t i = 0; i < num_fields_; i++) {
    int offset;
    int pos = fields_[i].pos;
    int attr_id = fields_[i].attr_id;

    if (attr_id < 0)
      offset = fields_[i].offset;
    else
      offset =
          mt_offset_to_databuf_offset(WildcardMatch::attr_offsets[attr_id]);

    char *key = keys[0] + pos;

    for (int j = 0; j < cnt; j++, key += HASH_KEY_SIZE) {
      char *buf_addr = (char *)batch->pkts[j]->mbuf.buf_addr;

      /* for offset-based attrs we use relative offset */
      if (attr_id < 0) buf_addr += batch->pkts[j]->mbuf.data_off;

      *(uint64_t *)key = *(uint64_t *)(buf_addr + offset);
    }
  }

#if 1
  for (int i = 0; i < cnt; i++)
    ogates[i] = LookupEntry((hkey_t *)keys[i], default_gate);
#else
  /* A version with an outer loop for tuples and an inner loop for pkts.
   * Significantly slower. */

  int priorities[MAX_PKT_BURST];
  const int key_size = total_key_size_;

  for (int i = 0; i < cnt; i++) {
    priorities[i] = INT_MIN;
    ogates[i] = default_gate;
  }

  for (int i = 0; i < num_tuples_; i++) {
    const struct WmTuple *tuple = &tuples_[i];
    const struct htable *ht = &tuple->ht;
    const hkey_t *tuple_mask = &tuple->mask;

    for (int j = 0; j < cnt; j++) {
      hkey_t key_masked;
      struct WmData *cand;

      mask(&key_masked, keys[j], tuple_mask, key_size);

      cand = ht->Get(&key_masked);

      if (cand && cand->priority >= priorities[j]) {
        ogates[j] = cand->ogate;
        priorities[j] = cand->priority;
      }
    }
  }
#endif

  run_split(this, ogates, batch);
}

struct snobj *WildcardMatch::GetDesc() {
  int num_rules = 0;

  for (int i = 0; i < num_tuples_; i++) num_rules += tuples_[i].ht.Count();

  return snobj_str_fmt("%lu fields, %d rules", num_fields_, num_rules);
}

struct snobj *WildcardMatch::GetDump() {
  struct snobj *r = snobj_map();
  struct snobj *fields = snobj_list();
  struct snobj *rules = snobj_list();

  for (size_t i = 0; i < num_fields_; i++) {
    struct snobj *f_obj = snobj_map();
    const struct WmField *f = &fields_[i];

    snobj_map_set(f_obj, "size", snobj_uint(f->size));

    if (f->attr_id < 0)
      snobj_map_set(f_obj, "offset", snobj_uint(f->offset));
    else
      snobj_map_set(f_obj, "name",
                    snobj_str(WildcardMatch::attrs[f->attr_id].name));

    snobj_list_add(fields, f_obj);
  }

  for (int k = 0; k < num_tuples_; k++) {
    const struct WmTuple *tuple = &tuples_[k];

    CollectRules(tuple, rules);
  }

  snobj_map_set(r, "fields", fields);
  snobj_map_set(r, "rules", rules);

  return r;
}

void WildcardMatch::CollectRules(const struct WmTuple *tuple,
                                 struct snobj *rules) {
  uint32_t next = 0;
  void *key;
  const void *mask = &tuple->mask;

  while ((key = tuple->ht.Iterate(&next))) {
    struct snobj *rule = snobj_map();
    struct snobj *values = snobj_list();
    struct snobj *masks = snobj_list();

    for (size_t i = 0; i < num_fields_; i++) {
      const struct WmField *f = &fields_[i];
      int pos = f->pos;
      int size = f->size;

      snobj_list_add(values,
                     snobj_blob(static_cast<uint8_t *>(key) + pos, size));
      snobj_list_add(
          masks,
          snobj_blob(reinterpret_cast<const uint8_t *>(mask) + pos, size));
    }

    snobj_map_set(rule, "values", values);
    snobj_map_set(rule, "masks", masks);
    snobj_list_add(rules, rule);
  }
}

struct snobj *WildcardMatch::ExtractKeyMask(struct snobj *arg, hkey_t *key,
                                            hkey_t *mask) {
  struct snobj *values;
  struct snobj *masks;

  if (snobj_type(arg) != TYPE_MAP)
    return snobj_err(EINVAL, "argument must be a map");

  values = snobj_eval(arg, "values");
  masks = snobj_eval(arg, "masks");

  if (!values || snobj_type(values) != TYPE_LIST || !snobj_size(values))
    return snobj_err(EINVAL, "'values' must be a list");

  if (values->size != num_fields_)
    return snobj_err(EINVAL, "must specify %lu values", num_fields_);

  if (!masks || snobj_type(masks) != TYPE_LIST)
    return snobj_err(EINVAL, "'masks' must be a list");

  if (masks->size != num_fields_)
    return snobj_err(EINVAL, "must specify %lu masks", num_fields_);

  memset(key, 0, sizeof(*key));
  memset(mask, 0, sizeof(*mask));

  for (size_t i = 0; i < values->size; i++) {
    int field_size = fields_[i].size;
    int field_pos = fields_[i].pos;

    struct snobj *v_obj = snobj_list_get(values, i);
    struct snobj *m_obj = snobj_list_get(masks, i);
    uint64_t v = 0;
    uint64_t m = 0;

    int force_be = (fields_[i].attr_id < 0);

    if (snobj_binvalue_get(v_obj, field_size, &v, force_be))
      return snobj_err(EINVAL, "idx %lu: not a correct %d-byte value", i,
                       field_size);

    if (snobj_binvalue_get(m_obj, field_size, &m, force_be))
      return snobj_err(EINVAL, "idx %lu: not a correct %d-byte mask", i,
                       field_size);

    if (v & ~m)
      return snobj_err(EINVAL,
                       "idx %lu: invalid pair of "
                       "value 0x%0*" PRIx64
                       " and "
                       "mask 0x%0*" PRIx64,
                       i, field_size * 2, v, field_size * 2, m);

    memcpy(reinterpret_cast<uint8_t *>(key) + field_pos, &v, field_size);
    memcpy(reinterpret_cast<uint8_t *>(mask) + field_pos, &m, field_size);
  }

  return NULL;
}

int WildcardMatch::FindTuple(hkey_t *mask) {
  int key_size = total_key_size_;

  for (int i = 0; i < num_tuples_; i++) {
    struct WmTuple *tuple = &tuples_[i];

    if (memcmp(&tuple->mask, mask, key_size) == 0) return i;
  }

  return -ENOENT;
}

int WildcardMatch::AddTuple(hkey_t *mask) {
  struct WmTuple *tuple;

  int ret;

  if (num_tuples_ >= MAX_TUPLES) return -ENOSPC;

  tuple = &tuples_[num_tuples_++];
  memcpy(&tuple->mask, mask, sizeof(*mask));

  ret = tuple->ht.Init(total_key_size_, sizeof(struct WmData));
  if (ret < 0) return ret;

  return tuple - tuples_;
}

int WildcardMatch::AddEntry(struct WmTuple *tuple, hkey_t *key,
                            struct WmData *data) {
  int ret;

  ret = tuple->ht.Set(key, data);
  if (ret < 0) return ret;

  return 0;
}

int WildcardMatch::DelEntry(struct WmTuple *tuple, hkey_t *key) {
  int ret = tuple->ht.Del(key);
  if (ret) return ret;

  if (tuple->ht.Count() == 0) {
    int idx = tuple - tuples_;

    tuple->ht.Close();

    num_tuples_--;
    memmove(&tuples_[idx], &tuples_[idx + 1],
            sizeof(*tuple) * (num_tuples_ - idx));
  }

  return 0;
}

struct snobj *WildcardMatch::CommandAdd(struct snobj *arg) {
  gate_idx_t gate = snobj_eval_uint(arg, "gate");
  int priority = snobj_eval_int(arg, "priority");

  hkey_t key;
  hkey_t mask;

  struct WmData data;

  struct snobj *err = ExtractKeyMask(arg, &key, &mask);
  if (err) return err;

  if (!snobj_eval_exists(arg, "gate"))
    return snobj_err(EINVAL, "'gate' must be specified");

  if (!is_valid_gate(gate)) return snobj_err(EINVAL, "Invalid gate: %hu", gate);

  data.priority = priority;
  data.ogate = gate;

  int idx = FindTuple(&mask);
  if (idx < 0) {
    idx = AddTuple(&mask);
    if (idx < 0) return snobj_err(-idx, "failed to add a new wildcard pattern");
  }

  int ret = AddEntry(&tuples_[idx], &key, &data);
  if (ret < 0) return snobj_err(-ret, "failed to add a rule");

  return NULL;
}

struct snobj *WildcardMatch::CommandDelete(struct snobj *arg) {
  hkey_t key;
  hkey_t mask;

  struct snobj *err = ExtractKeyMask(arg, &key, &mask);
  if (err) return err;

  int idx = FindTuple(&mask);
  if (idx < 0) return snobj_err(-idx, "failed to delete a rule");

  int ret = DelEntry(&tuples_[idx], &key);
  if (ret < 0) return snobj_err(-ret, "failed to delete a rule");

  return NULL;
}

struct snobj *WildcardMatch::CommandClear(struct snobj *arg) {
  for (int i = 0; i < num_tuples_; i++) tuples_[i].ht.Clear();

  return NULL;
}

struct snobj *WildcardMatch::CommandSetDefaultGate(struct snobj *arg) {
  int gate = snobj_int_get(arg);

  default_gate_ = gate;

  return NULL;
}

ADD_MODULE(WildcardMatch, "wm",
           "Multi-field classifier with a wildcard match table")
