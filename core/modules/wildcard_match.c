#include "../module.h"

#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_errno.h>

#define DEFAULT_TABLE_SIZE	1024
#define MAX_FIELDS		8
#define MAX_TUPLES		8

#define MAX_FIELD_SIZE		8
ct_assert(MAX_FIELD_SIZE <= sizeof(uint64_t));

#define HASH_KEY_SIZE		(MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
  #error this code assumes little endian architecture (x86)
#endif

#if __SSE4_2__
  #include <rte_hash_crc.h>
  #define DEFAULT_HASH_FUNC       rte_hash_crc
#else
  #include <rte_jhash.h>
  #define DEFAULT_HASH_FUNC       rte_jhash
#endif

struct data {
	int priority;
	gate_idx_t ogate;
};

struct wm_priv {
	int max_rules;
	int num_rules;

	gate_idx_t default_gate;

	int total_key_size;	/* a multiple of sizeof(uint64_t) */

	int num_fields;
	struct field {
		int attr_id;	/* -1 for offset-based fields */

		/* Relative offset in the packet data for offset-based fields.
		 *  (starts from data_off, not the beginning of the headroom */
		int16_t offset;

		uint8_t size_acc;

		uint8_t size;	/* in bytes. 1 <= size <= MAX_FIELD_SIZE */
	} fields[MAX_FIELDS];

	int num_tuples;
	struct tuple {
		struct rte_hash *tbl;
		int num_rules;
		char mask[HASH_KEY_SIZE];
	} tuples[MAX_TUPLES];

	int next_table_id;
};

static int uint_to_bin(uint8_t *ptr, int size, uint64_t val, int be)
{
	if (be) {
		for (int i = size - 1; i >= 0; i--) {
			ptr[i] = val & 0xff;
			val >>= 8;
		}
	} else {
		for (int i = 0; i < size; i++) {
			ptr[i] = val & 0xff;
			val >>= 8;
		}
	}

	if (val)
		return -EINVAL;	/* the value is too large for the size */
	else
		return 0;
}

/* ptr must be big enough to hold 'size' bytes.
 * If be is non-zero and the varible is given as an integer, 
 * its value will be stored in big endian */
static int get_binary_value(struct snobj *var, int size, void *ptr, int be)
{
	if (!var || size < 1)
		return -EINVAL;

	switch (snobj_type(var)) {
		case TYPE_BLOB:
			if (var->size != size)
				return -EINVAL;
			memcpy(ptr, snobj_blob_get(var), var->size);
			return 0;

		case TYPE_STR:
			if (var->size != size + 1)
				return -EINVAL;
			memcpy(ptr, snobj_str_get(var), var->size);
			return 0;

		case TYPE_INT:
			return uint_to_bin(ptr, size, snobj_uint_get(var), be);

		default:
			return -EINVAL;
	}
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
		if (f->offset < 0)
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
	int8_t size_acc = 0;

	struct snobj *fields = snobj_eval(arg, "fields");

	if (snobj_type(fields) != TYPE_LIST)
		return snobj_err(EINVAL, "'fields' must be a list of maps");

	for (int i = 0; i < fields->size; i++) {
		struct snobj *field = snobj_list_get(fields, i);
		struct snobj *err;
		struct field f;

		f.size_acc = size_acc;

		err = add_field_one(m, field, &f);
		if (err)
			return err;

		size_acc += f.size;
		priv->fields[i] = f;
	}

	priv->default_gate = DROP_GATE;
	priv->num_fields = fields->size;
	priv->total_key_size = align_ceil(size_acc, sizeof(uint64_t));
	priv->max_rules = DEFAULT_TABLE_SIZE;

	/* hash table size is given? */
	if (snobj_eval_exists(arg, "size")) {
		uint32_t size = snobj_eval_uint(arg, "size");
		if (size == 0)
			return snobj_err(EINVAL, "invalid table size");
		priv->max_rules = size;
	}

	return NULL;
}

static void wm_deinit(struct module *m)
{
	struct wm_priv *priv = get_priv(m);

	for (int i = 0; i < priv->num_tuples; i++)
		rte_hash_free(priv->tuples[i].tbl);
}

/* k1 = k2 & mask */
static void mask(void *k1, void *k2, void *mask, int key_size)
{
	uint64_t *a = k1;
	uint64_t *b = k2;
	uint64_t *m = mask;

	switch (key_size >> 3) {
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

static gate_idx_t lookup_entry(struct wm_priv *priv, char *key, 
		gate_idx_t def_gate)
{
	struct data result = {
		.priority = INT_MIN, 
		.ogate = def_gate,
	};

	const int key_size = priv->total_key_size;
	const int num_tuples = priv->num_tuples;

	char key_masked[HASH_KEY_SIZE];

	for (int i = 0; i < num_tuples; i++) {
		struct tuple *tuple = &priv->tuples[i];
		struct data cand;
		int ret;

		mask(key_masked, key, tuple->mask, key_size);

		ret = rte_hash_lookup_with_hash_data(tuple->tbl,
				(const void *)key_masked,
				DEFAULT_HASH_FUNC(key_masked, key_size, 0),
				(void **)&cand);

		if (ret >= 0 && cand.priority >= result.priority)
			result = cand;
	}

	return result.ogate;
}

static void wm_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct wm_priv *priv = get_priv(m);

	gate_idx_t default_gate;
	gate_idx_t ogates[MAX_PKT_BURST];

	char keys[MAX_PKT_BURST][HASH_KEY_SIZE];

	int cnt = batch->cnt;

	default_gate = ACCESS_ONCE(priv->default_gate);

	/* initialize the last uint64_t word */
	for (int i = 0; i < cnt; i++) {
		char *key = keys[0] + priv->total_key_size - 8;
		*(uint64_t *)key = 0;
	}

	for (int i = 0; i < priv->num_fields; i++) {
		int offset;
		int size_acc = priv->fields[i].size_acc;
		int attr_id = priv->fields[i].attr_id;

		if (attr_id < 0)
			offset = priv->fields[i].offset;
		else
			offset = mt_offset_to_databuf_offset(
					mt_attr_offset(m, attr_id));

		char *key = keys[0] + size_acc;

		for (int j = 0; j < cnt; j++, key += HASH_KEY_SIZE) {
			char *buf_addr = (char *)batch->pkts[j]->mbuf.buf_addr;

			/* for offset-based attrs we use relative offset */
			if (attr_id < 0)
				buf_addr += batch->pkts[j]->mbuf.data_off;

			*(uint64_t *)key = *(uint64_t *)(buf_addr + offset);
		}
	}

	for (int i = 0; i < cnt; i++)
		ogates[i] = lookup_entry(priv, keys[i], default_gate);

	run_split(m, ogates, batch);
}

static struct snobj *
extract_key_mask(struct wm_priv *priv, struct snobj *arg, char *key, char *mask)
{
	struct snobj *values;
	struct snobj *masks;

	if (snobj_type(arg) != TYPE_MAP)
		return snobj_err(EINVAL, "argument must be a map");

	values = snobj_eval(arg, "values");
	masks = snobj_eval(arg, "masks");

	if (!values || snobj_type(values) != TYPE_LIST)
		return snobj_err(EINVAL, "'values' must be a list");

	if (values->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d values", 
				priv->num_fields);

	if (!masks || snobj_type(masks) != TYPE_LIST)
		return snobj_err(EINVAL, "'masks' must be a list");

	if (masks->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d masks", 
				priv->num_fields);

	memset(key, 0, HASH_KEY_SIZE);
	memset(mask, 0, HASH_KEY_SIZE);

	for (int i = 0; i < values->size; i++) {
		int field_size = priv->fields[i].size;
		int field_size_acc = priv->fields[i].size_acc;

		struct snobj *v_obj = snobj_list_get(values, i);
		struct snobj *m_obj = snobj_list_get(masks, i);
		uint64_t v = 0;
		uint64_t m = 0;

		int be = is_be_system() ? 1 : (priv->fields[i].attr_id < 0); 

		if (get_binary_value(v_obj, field_size, &v, be))
			return snobj_err(EINVAL, 
					"idx %d: not a correct %d-byte value",
					i, field_size);

		if (get_binary_value(m_obj, field_size, &m, be))
			return snobj_err(EINVAL, 
					"idx %d: not a correct %d-byte mask",
					i, field_size);

		if (v & ~m)
			return snobj_err(EINVAL,
					"idx %d: invalid pair of "
					"value 0x%0*lx and mask 0x%0*lx",
					i, 
					field_size * 2, v, field_size * 2, m);

		memcpy(key + field_size_acc, &v, field_size);
		memcpy(mask + field_size_acc, &m, field_size);
	}

	return NULL;
}

static int find_tuple(struct module *m, char *mask)
{
	struct wm_priv *priv = get_priv(m);

	int key_size = priv->total_key_size;

	for (int i = 0; i < priv->num_tuples; i++) {
		struct tuple *tuple = &priv->tuples[i];

		if (memcmp(tuple->mask, mask, key_size) == 0)
			return i;
	}

	return -ENOENT;
}

static int add_tuple(struct module *m, char *mask)
{
	struct wm_priv *priv = get_priv(m);

	struct tuple *tuple;
	struct rte_hash_parameters params;

	char ht_name[RTE_HASH_NAMESIZE];

	if (priv->num_tuples >= MAX_TUPLES)
		return -ENOSPC;

	snprintf(ht_name, sizeof(ht_name), "%s_%d", 
			m->name, priv->next_table_id++);

	tuple = &priv->tuples[priv->num_tuples++];
	tuple->num_rules = 0;
	memcpy(tuple->mask, mask, HASH_KEY_SIZE);

	params = (struct rte_hash_parameters) {
		.name = ht_name,
		.entries = priv->max_rules,
		.key_len = priv->total_key_size,
		.hash_func = DEFAULT_HASH_FUNC,
		.hash_func_init_val = 0,
		.socket_id = 0,		/* XXX */
	};

	tuple->tbl = rte_hash_create(&params);
	if (!tuple->tbl)
		return -rte_errno;

	return tuple - priv->tuples;
}

static void *data_to_ptr(struct data *data)
{
	uintptr_t *ptr = (uintptr_t *)data;

	return (void *)*ptr;
}

static int add_entry(struct tuple *tuple, char *key, struct data *data)
{
	int ret;
	int already_exist = (rte_hash_lookup(tuple->tbl, key) >= 0);
	
	ret = rte_hash_add_key_data(tuple->tbl, key, data_to_ptr(data));
	if (ret)
		return ret;

	if (!already_exist)
		tuple->num_rules++;

	return 0;
}

static int del_entry(struct wm_priv *priv, struct tuple *tuple, char *key)
{
	int ret = rte_hash_del_key(tuple->tbl, key);
	if (ret)
		return ret;

	tuple->num_rules--;

	if (tuple->num_rules == 0) {
		int idx = tuple - priv->tuples;

		rte_hash_free(tuple->tbl);

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

	char key[HASH_KEY_SIZE];
	char mask[HASH_KEY_SIZE];

	struct data data;

	struct snobj *err = extract_key_mask(priv, arg, key, mask);
	if (err)
		return err;

	if (priv->num_rules >= priv->max_rules)
		return snobj_err(ENOSPC, "table is full\n");

	if (!snobj_eval_exists(arg, "gate"))
		return snobj_err(EINVAL, 
				"'gate' must be specified");

	if (!is_valid_gate(gate))
		return snobj_err(EINVAL, "Invalid gate: %hu", gate);

	data = (struct data){
		.priority = priority,
		.ogate = gate,
	};

	int idx = find_tuple(m, mask);
	if (idx < 0) {
		idx = add_tuple(m, mask);
		if (idx < 0)
			return snobj_err(-idx,
					"failed to add a new wildcard pattern");
	}

	int ret = add_entry(&priv->tuples[idx], key, &data);
	if (ret < 0)
		return snobj_err(-ret, "failed to add a rule");

	return NULL;
}

static struct snobj *
command_delete(struct module *m, const char *cmd, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);

	char key[HASH_KEY_SIZE];
	char mask[HASH_KEY_SIZE];

	struct snobj *err = extract_key_mask(priv, arg, key, mask);
	if (err)
		return err;

	int idx = find_tuple(m, mask);
	if (idx < 0)
		return snobj_err(-idx, "failed to delete a rule");

	int ret = del_entry(priv, &priv->tuples[idx], key);
	if (ret < 0)
		return snobj_err(-ret, "failed to delete a rule");

	return NULL;
}

static struct snobj *
command_clear(struct module *m, const char *cmd, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);

	priv->num_rules = 0;

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
	.commands		= {
		{"add", 		command_add},
		{"delete", 		command_delete},
		{"clear", 		command_clear},
		{"set_default_gate",	command_set_default_gate, .mt_safe=1},
	}
};

ADD_MCLASS(wm)
