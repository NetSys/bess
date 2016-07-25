#include "../utils/htable.h"

#include "../module.h"

#define MAX_TUPLES		8
#define MAX_FIELDS		8
#define MAX_FIELD_SIZE		8
ct_assert(MAX_FIELD_SIZE <= sizeof(uint64_t));

#define HASH_KEY_SIZE		(MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
  #error this code assumes little endian architecture (x86)
#endif

typedef struct {
	uint64_t u64_arr[MAX_FIELDS];
} hkey_t;

HT_DECLARE_INLINED_FUNCS(wm, hkey_t)

struct data {
	int priority;
	gate_idx_t ogate;
};

struct wm_priv {
	gate_idx_t default_gate;

	int total_key_size;	/* a multiple of sizeof(uint64_t) */

	int num_fields;
	struct field {
		int attr_id;	/* -1 for offset-based fields */

		/* Relative offset in the packet data for offset-based fields.
		 *  (starts from data_off, not the beginning of the headroom */
		int offset;

		int pos;	/* relative position in the key */

		int size;	/* in bytes. 1 <= size <= MAX_FIELD_SIZE */
	} fields[MAX_FIELDS];

	int num_tuples;
	struct tuple {
		struct htable ht;
		hkey_t mask;
	} tuples[MAX_TUPLES];

	int next_table_id;
};

static inline int
wm_keycmp(const hkey_t *key, const hkey_t *key_stored, size_t key_len)
{
	const uint64_t *a = key->u64_arr;
	const uint64_t *b = key_stored->u64_arr;

	switch (key_len >> 3) {
	default: promise_unreachable();
	case 8: if (unlikely(a[7] != b[7])) return 1;
	case 7: if (unlikely(a[6] != b[6])) return 1;
	case 6: if (unlikely(a[5] != b[5])) return 1;
	case 5: if (unlikely(a[4] != b[4])) return 1;
	case 4: if (unlikely(a[3] != b[3])) return 1;
	case 3: if (unlikely(a[2] != b[2])) return 1;
	case 2: if (unlikely(a[1] != b[1])) return 1;
	case 1: if (unlikely(a[0] != b[0])) return 1;
	}

	return 0;
}

static inline uint32_t
wm_hash(const hkey_t *key, uint32_t key_len, uint32_t init_val)
{
#if __SSE4_2__
	const uint64_t *a = key->u64_arr;

	switch (key_len >> 3) {
	default: promise_unreachable();
	case 8: init_val = crc32c_sse42_u64(*a++, init_val);
	case 7: init_val = crc32c_sse42_u64(*a++, init_val);
	case 6: init_val = crc32c_sse42_u64(*a++, init_val);
	case 5: init_val = crc32c_sse42_u64(*a++, init_val);
	case 4: init_val = crc32c_sse42_u64(*a++, init_val);
	case 3: init_val = crc32c_sse42_u64(*a++, init_val);
	case 2: init_val = crc32c_sse42_u64(*a++, init_val);
	case 1: init_val = crc32c_sse42_u64(*a++, init_val);
	}

	return init_val;
#else
	return rte_hash_crc(key, key_len, init_val);
#endif
}

static struct snobj *
add_field_one(struct module *m, struct snobj *field, struct field *f)
{
	if (field->type != TYPE_MAP)
		return snobj_err(EINVAL,
				"'fields' must be a list of maps");

	f->size = snobj_eval_uint(field, "size");

	if (f->size < 1 || f->size > MAX_FIELD_SIZE)
		return snobj_err(EINVAL, "'size' must be 1-%d",
				MAX_FIELD_SIZE);

	if (snobj_eval_exists(field, "offset")) {
		f->attr_id = -1;
		f->offset = snobj_eval_int(field, "offset");
		if (f->offset < 0 || f->offset > 1024)
			return snobj_err(EINVAL, "too small 'offset'");
		return NULL;
	}

	const char *attr_name = snobj_eval_str(field, "name");
	if (!attr_name)
		return snobj_err(EINVAL, "specify 'offset' or 'name'");

	f->attr_id = add_metadata_attr(m, attr_name, f->size, MT_READ);
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
static struct snobj *wm_init(struct module *m, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);
	int size_acc = 0;

	struct snobj *fields = snobj_eval(arg, "fields");

	if (snobj_type(fields) != TYPE_LIST)
		return snobj_err(EINVAL, "'fields' must be a list of maps");

	for (int i = 0; i < fields->size; i++) {
		struct snobj *field = snobj_list_get(fields, i);
		struct snobj *err;
		struct field f;

		f.pos = size_acc;

		err = add_field_one(m, field, &f);
		if (err)
			return err;

		size_acc += f.size;
		priv->fields[i] = f;
	}

	priv->default_gate = DROP_GATE;
	priv->num_fields = fields->size;
	priv->total_key_size = align_ceil(size_acc, sizeof(uint64_t));

	return NULL;
}

static void wm_deinit(struct module *m)
{
	struct wm_priv *priv = get_priv(m);

	for (int i = 0; i < priv->num_tuples; i++)
		ht_close(&priv->tuples[i].ht);
}

/* k1 = k2 & mask */
static void mask(void *k1, const void *k2, const void *mask, int key_size)
{
	uint64_t * restrict a = k1;
	const uint64_t * restrict b = k2;
	const uint64_t * restrict m = mask;

	switch (key_size >> 3) {
	default: promise_unreachable();
	case 8: a[7] = b[7] & m[7];
	case 7: a[6] = b[6] & m[6];
	case 6: a[5] = b[5] & m[5];
	case 5: a[4] = b[4] & m[4];
	case 4: a[3] = b[3] & m[3];
	case 3: a[2] = b[2] & m[2];
	case 2: a[1] = b[1] & m[1];
	case 1: a[0] = b[0] & m[0];
	}
}

static gate_idx_t
lookup_entry(struct wm_priv *priv, hkey_t *key, gate_idx_t def_gate)
{
	struct data result = {
		.priority = INT_MIN,
		.ogate = def_gate,
	};

	const int key_size = priv->total_key_size;
	const int num_tuples = priv->num_tuples;

	hkey_t key_masked;

	for (int i = 0; i < num_tuples; i++) {
		struct tuple *tuple = &priv->tuples[i];
		struct data *cand;

		mask(&key_masked, key, &tuple->mask, key_size);

		cand = ht_wm_get(&tuple->ht, &key_masked);

		if (cand && cand->priority >= result.priority)
			result = *cand;
	}

	return result.ogate;
}

static void wm_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct wm_priv *priv = get_priv(m);

	gate_idx_t default_gate;
	gate_idx_t ogates[MAX_PKT_BURST];

	char keys[MAX_PKT_BURST][HASH_KEY_SIZE] __ymm_aligned;

	int cnt = batch->cnt;

	default_gate = ACCESS_ONCE(priv->default_gate);

	for (int i = 0; i < priv->num_fields; i++) {
		int offset;
		int pos = priv->fields[i].pos;
		int attr_id = priv->fields[i].attr_id;

		if (attr_id < 0)
			offset = priv->fields[i].offset;
		else
			offset = mt_offset_to_databuf_offset(
					mt_attr_offset(m, attr_id));

		char *key = keys[0] + pos;

		for (int j = 0; j < cnt; j++, key += HASH_KEY_SIZE) {
			char *buf_addr = (char *)batch->pkts[j]->mbuf.buf_addr;

			/* for offset-based attrs we use relative offset */
			if (attr_id < 0)
				buf_addr += batch->pkts[j]->mbuf.data_off;

			*(uint64_t *)key = *(uint64_t *)(buf_addr + offset);
		}
	}

#if 1
	for (int i = 0; i < cnt; i++)
		ogates[i] = lookup_entry(priv, (hkey_t *)keys[i], default_gate);
#else
	/* A version with an outer loop for tuples and an inner loop for pkts.
	 * Significantly slower. */

	int priorities[MAX_PKT_BURST];
	const int key_size = priv->total_key_size;

	for (int i = 0; i < cnt; i++) {
		priorities[i] = INT_MIN;
		ogates[i] = default_gate;
	}

	for (int i = 0; i < priv->num_tuples; i++) {
		const struct tuple *tuple = &priv->tuples[i];
		const struct htable *ht = &tuple->ht;
		const hkey_t *tuple_mask = &tuple->mask;

		for (int j = 0; j < cnt; j++) {
			hkey_t key_masked;
			struct data *cand;

			mask(&key_masked, keys[j], tuple_mask, key_size);

			cand = ht_wm_get(ht, &key_masked);

			if (cand && cand->priority >= priorities[j]) {
				ogates[j] = cand->ogate;
				priorities[j] = cand->priority;
			}
		}
	}
#endif

	run_split(m, ogates, batch);
}

static struct snobj *wm_get_desc(const struct module *m)
{
	const struct wm_priv *priv = get_priv_const(m);
	int num_rules = 0;

	for (int i = 0; i < priv->num_tuples; i++)
		num_rules += priv->tuples[i].ht.cnt;

	return snobj_str_fmt("%d fields, %d rules",
			priv->num_fields, num_rules);
}

static void collect_rules(const struct wm_priv *priv, const struct tuple *tuple,
		struct snobj *rules)
{
	uint32_t next = 0;
	void *key;
	const void *mask = &tuple->mask;

	while ((key = ht_iterate(&tuple->ht, &next))) {
		struct snobj *rule = snobj_map();
		struct snobj *values = snobj_list();
		struct snobj *masks = snobj_list();

		for (int i = 0; i < priv->num_fields; i++) {
			const struct field *f = &priv->fields[i];
			int pos = f->pos;
			int size = f->size;

			snobj_list_add(values, snobj_blob(key + pos, size));
			snobj_list_add(masks, snobj_blob(mask + pos, size));

		}

		snobj_map_set(rule, "values", values);
		snobj_map_set(rule, "masks", masks);
		snobj_list_add(rules, rule);
	}
}

static struct snobj *wm_get_dump(const struct module *m)
{
	const struct wm_priv *priv = get_priv_const(m);

	struct snobj *r = snobj_map();
	struct snobj *fields = snobj_list();
	struct snobj *rules = snobj_list();

	for (int i = 0; i < priv->num_fields; i++) {
		struct snobj *f_obj = snobj_map();
		const struct field *f = &priv->fields[i];

		snobj_map_set(f_obj, "size", snobj_uint(f->size));

		if (f->attr_id < 0)
			snobj_map_set(f_obj, "offset", snobj_uint(f->offset));
		else
			snobj_map_set(f_obj, "name",
					snobj_str(m->attrs[f->attr_id].name));

		snobj_list_add(fields, f_obj);
	}

	for (int k = 0; k < priv->num_tuples; k++) {
		const struct tuple *tuple = &priv->tuples[k];

		collect_rules(priv, tuple, rules);
	}

	snobj_map_set(r, "fields", fields);
	snobj_map_set(r, "rules", rules);

	return r;
}

static struct snobj *
extract_key_mask(struct wm_priv *priv, struct snobj *arg,
		hkey_t *key, hkey_t *mask)
{
	struct snobj *values;
	struct snobj *masks;

	if (snobj_type(arg) != TYPE_MAP)
		return snobj_err(EINVAL, "argument must be a map");

	values = snobj_eval(arg, "values");
	masks = snobj_eval(arg, "masks");

	if (!values || snobj_type(values) != TYPE_LIST || !snobj_size(values))
		return snobj_err(EINVAL, "'values' must be a list");

	if (values->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d values",
				priv->num_fields);

	if (!masks || snobj_type(masks) != TYPE_LIST)
		return snobj_err(EINVAL, "'masks' must be a list");

	if (masks->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d masks",
				priv->num_fields);

	memset(key, 0, sizeof(*key));
	memset(mask, 0, sizeof(*mask));

	for (int i = 0; i < values->size; i++) {
		int field_size = priv->fields[i].size;
		int field_pos = priv->fields[i].pos;

		struct snobj *v_obj = snobj_list_get(values, i);
		struct snobj *m_obj = snobj_list_get(masks, i);
		uint64_t v = 0;
		uint64_t m = 0;

		int force_be = (priv->fields[i].attr_id < 0);

		if (snobj_binvalue_get(v_obj, field_size, &v, force_be))
			return snobj_err(EINVAL,
					"idx %d: not a correct %d-byte value",
					i, field_size);

		if (snobj_binvalue_get(m_obj, field_size, &m, force_be))
			return snobj_err(EINVAL,
					"idx %d: not a correct %d-byte mask",
					i, field_size);

		if (v & ~m)
			return snobj_err(EINVAL,
					"idx %d: invalid pair of "
					"value 0x%0*lx and mask 0x%0*lx",
					i,
					field_size * 2, v, field_size * 2, m);

		memcpy((void *)key + field_pos, &v, field_size);
		memcpy((void *)mask + field_pos, &m, field_size);
	}

	return NULL;
}

static int find_tuple(struct module *m, hkey_t *mask)
{
	struct wm_priv *priv = get_priv(m);

	int key_size = priv->total_key_size;

	for (int i = 0; i < priv->num_tuples; i++) {
		struct tuple *tuple = &priv->tuples[i];

		if (memcmp(&tuple->mask, mask, key_size) == 0)
			return i;
	}

	return -ENOENT;
}

static int add_tuple(struct module *m, hkey_t *mask)
{
	struct wm_priv *priv = get_priv(m);

	struct tuple *tuple;

	int ret;

	if (priv->num_tuples >= MAX_TUPLES)
		return -ENOSPC;

	tuple = &priv->tuples[priv->num_tuples++];
	memcpy(&tuple->mask, mask, sizeof(*mask));

	ret = ht_init(&tuple->ht, priv->total_key_size, sizeof(struct data));
	if (ret < 0)
		return ret;

	return tuple - priv->tuples;
}

static int add_entry(struct tuple *tuple, hkey_t *key, struct data *data)
{
	int ret;

	ret = ht_set(&tuple->ht, key, data);
	if (ret < 0)
		return ret;

	return 0;
}

static int del_entry(struct wm_priv *priv, struct tuple *tuple, hkey_t *key)
{
	int ret = ht_del(&tuple->ht, key);
	if (ret)
		return ret;

	if (tuple->ht.cnt == 0) {
		int idx = tuple - priv->tuples;

		ht_close(&tuple->ht);

		priv->num_tuples--;
		memmove(&priv->tuples[idx], &priv->tuples[idx + 1],
				sizeof(*tuple) * (priv->num_tuples - idx));
	}

	return 0;
}

static struct snobj *
command_add(struct module *m, const char *cmd, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);

	gate_idx_t gate = snobj_eval_uint(arg, "gate");
	int priority = snobj_eval_int(arg, "priority");

	hkey_t key;
	hkey_t mask;

	struct data data;

	struct snobj *err = extract_key_mask(priv, arg, &key, &mask);
	if (err)
		return err;

	if (!snobj_eval_exists(arg, "gate"))
		return snobj_err(EINVAL,
				"'gate' must be specified");

	if (!is_valid_gate(gate))
		return snobj_err(EINVAL, "Invalid gate: %hu", gate);

	data = (struct data){
		.priority = priority,
		.ogate = gate,
	};

	int idx = find_tuple(m, &mask);
	if (idx < 0) {
		idx = add_tuple(m, &mask);
		if (idx < 0)
			return snobj_err(-idx,
					"failed to add a new wildcard pattern");
	}

	int ret = add_entry(&priv->tuples[idx], &key, &data);
	if (ret < 0)
		return snobj_err(-ret, "failed to add a rule");

	return NULL;
}

static struct snobj *
command_delete(struct module *m, const char *cmd, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);

	hkey_t key;
	hkey_t mask;

	struct snobj *err = extract_key_mask(priv, arg, &key, &mask);
	if (err)
		return err;

	int idx = find_tuple(m, &mask);
	if (idx < 0)
		return snobj_err(-idx, "failed to delete a rule");

	int ret = del_entry(priv, &priv->tuples[idx], &key);
	if (ret < 0)
		return snobj_err(-ret, "failed to delete a rule");

	return NULL;
}

static struct snobj *
command_clear(struct module *m, const char *cmd, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);

	for (int i = 0; i < priv->num_tuples; i++)
		ht_clear(&priv->tuples[i].ht);

	return NULL;
}

static struct snobj *
command_set_default_gate(struct module *m, const char *cmd, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);

	int gate = snobj_int_get(arg);

	priv->default_gate = gate;

	return NULL;
}

static const struct mclass wm = {
	.name 			= "WildcardMatch",
	.help			=
		"Multi-field classifier with a wildcard match table",
	.def_module_name	= "wm",
	.num_igates		= 1,
	.num_ogates		= MAX_GATES,
	.priv_size		= sizeof(struct wm_priv),
	.init 			= wm_init,
	.deinit          	= wm_deinit,
	.process_batch 		= wm_process_batch,
	.get_desc		= wm_get_desc,
	.get_dump		= wm_get_dump,
	.commands		= {
		{"add", 		command_add},
		{"delete", 		command_delete},
		{"clear", 		command_clear},
		{"set_default_gate",	command_set_default_gate, .mt_safe=1},
	}
};

ADD_MCLASS(wm)
