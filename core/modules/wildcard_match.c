#include "../module.h"

#include <rte_hash.h>
#include <rte_jhash.h>

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

struct rule {
	int priority;		/* higher number == higher priority */
	gate_idx_t gate;
	char key[HASH_KEY_SIZE];
	char mask[HASH_KEY_SIZE];
};

struct wm_priv {
	struct rule *rules;
	int tbl_size;

	gate_idx_t default_gate;

	uint32_t total_key_size;

	int num_fields;
	struct field {
		int attr_id;	/* -1 for offset-based fields */

		/* Relative offset in the packet data for offset-based fields.
		 *  (starts from data_off, not the beginning of the headroom */
		int16_t offset;

		uint8_t size_acc;

		uint8_t size;	/* in bytes. 1 <= size <= MAX_FIELD_SIZE */
	} fields[MAX_FIELDS];

	int num_rules;
};

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

/* Takes a list of fields. Each field needs 'offset' (or 'name') and 'size', 
 * and optional "mask" (0xfffff.. by default)
 *
 * e.g.: ExactMatch([{'offset': 14, 'size': 1, 'mask':0xf0}, ...] 
 * (checks the IP version field)
 *
 * You can also specify metadata attributes
 * e.g.: ExactMatch([{'name': 'nexthop', 'size': 4}, ...] */
static struct snobj *wm_init(struct module *m, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);
	int8_t size_acc = 0;

	struct snobj *fields = snobj_eval(arg, "fields");

	struct rte_hash_parameters params;

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
	priv->total_key_size = size_acc;
	priv->tbl_size = DEFAULT_TABLE_SIZE;

	/* hash table size is given? */
	if (snobj_eval_exists(arg, "size")) {
		uint32_t size = snobj_eval_uint(arg, "size");
		if (size == 0)
			return snobj_err(EINVAL, "invalid table size");
		priv->tbl_size = size;
	}

	params = (struct rte_hash_parameters) {
		.name = m->name,
		.entries = priv->tbl_size,
		.key_len = size_acc,
		.hash_func = DEFAULT_HASH_FUNC,
		.hash_func_init_val = 0,
		.socket_id = 0,		/* XXX */
	};

	priv->rules = mem_alloc(priv->tbl_size * sizeof(struct rule));
	if (!priv->rules)
		return snobj_err(ENOMEM, "out of memory");

	return NULL;
}

static void wm_deinit(struct module *m)
{
	struct wm_priv *priv = get_priv(m);

	mem_free(priv->rules);
}

/* slowest possible implementation */
static int match_entry(struct wm_priv *priv, char *key)
{
	int key_size = priv->total_key_size;
	struct rule *found = NULL;

	for (int i = 0; i < priv->num_rules; i++) {
		struct rule *rule = &priv->rules[i];
		int matched = 1;

		for (int j = 0; j < key_size; j++)
			if ((key[j] & rule->mask[j]) != rule->key[j])
				matched = 0;

		if (matched && (!found || found->priority < rule->priority))
			found = rule;
	}

	if (!found)
		return -ENOENT;
	else
		return found - priv->rules;
}

static int find_entry(struct wm_priv *priv, char *key, char *mask)
{
	int key_size = priv->total_key_size;

	for (int i = 0; i < priv->num_rules; i++) {
		struct rule *rule = &priv->rules[i];

		if (memcmp(rule->key, key, key_size) == 0 &&
				memcmp(rule->mask, mask, key_size) == 0)
			return i;
	}

	return -ENOENT;
}

static void wm_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct wm_priv *priv = get_priv(m);

	gate_idx_t default_gate;
	gate_idx_t ogates[MAX_PKT_BURST];

	char keys[MAX_PKT_BURST][HASH_KEY_SIZE];

	int cnt = batch->cnt;

	default_gate = ACCESS_ONCE(priv->default_gate);

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

	for (int i = 0; i < cnt; i++) {
		uintptr_t result = default_gate;
		int ret;
	
		ret = match_entry(priv, keys[i]);
		if (ret >= 0)
			result = priv->rules[ret].gate;

		ogates[i] = result;
	}

	run_split(m, ogates, batch);
}

static struct snobj *
command_add(struct module *m, const char *cmd, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);

	struct snobj *fields = snobj_eval(arg, "fields");
	struct snobj *masks = snobj_eval(arg, "masks");
	gate_idx_t gate = snobj_eval_uint(arg, "gate");
	int priority = snobj_eval_int(arg, "priority");

	char key[HASH_KEY_SIZE];
	char mask[HASH_KEY_SIZE];

	int idx;

	if (priv->num_rules >= priv->tbl_size)
		return snobj_err(ENOSPC, "table is full\n");

	if (!snobj_eval_exists(arg, "gate"))
		return snobj_err(EINVAL, 
				"'gate' must be specified");

	if (!is_valid_gate(gate))
		return snobj_err(EINVAL, "Invalid gate: %hu", gate);

	if (!fields || snobj_type(fields) != TYPE_LIST)
		return snobj_err(EINVAL, "'fields' must be a list of blobs");

	if (fields->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d fields", 
				priv->num_fields);

	if (!masks || snobj_type(masks) != TYPE_LIST)
		return snobj_err(EINVAL, "'masks' must be a list of blobs");

	if (masks->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d masks", 
				priv->num_fields);

	for (int i = 0; i < fields->size; i++) {
		struct snobj *field_val = snobj_list_get(fields, i);
		struct snobj *mask_val = snobj_list_get(masks, i);
		uint64_t *p1;
		uint64_t *p2;

		if (snobj_type(field_val) != TYPE_BLOB ||
				field_val->size != priv->fields[i].size)
			return snobj_err(EINVAL, 
					"field %d must be BLOB of %d bytes",
					i, priv->fields[i].size);

		if (snobj_type(mask_val) != TYPE_BLOB ||
				mask_val->size != priv->fields[i].size)
			return snobj_err(EINVAL, 
					"mask %d must be BLOB of %d bytes",
					i, priv->fields[i].size);

		p1 = snobj_blob_get(field_val);
		p2 = snobj_blob_get(mask_val);
		*(uint64_t *)(key + priv->fields[i].size_acc) = *p1;
		*(uint64_t *)(mask + priv->fields[i].size_acc) = *p2;
	}

	idx = find_entry(priv, key, mask);
	if (idx < 0)
		idx = priv->num_rules++;

	struct rule *rule = &priv->rules[priv->num_rules++];
	rule->priority = priority;
	rule->gate = gate;
	memcpy(rule->key, key, priv->total_key_size);
	memcpy(rule->mask, mask, priv->total_key_size);

	return NULL;
}

static struct snobj *
command_delete(struct module *m, const char *cmd, struct snobj *arg)
{
	struct wm_priv *priv = get_priv(m);

	struct snobj *fields = snobj_eval(arg, "fields");
	struct snobj *masks = snobj_eval(arg, "masks");

	char key[HASH_KEY_SIZE];
	char mask[HASH_KEY_SIZE];

	int idx;

	if (!arg || snobj_type(arg) != TYPE_LIST)
		return snobj_err(EINVAL, "argument must be a list of blobs");

	if (!fields || snobj_type(fields) != TYPE_LIST)
		return snobj_err(EINVAL, "'fields' must be a list of blobs");

	if (fields->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d fields", 
				priv->num_fields);

	if (!masks || snobj_type(masks) != TYPE_LIST)
		return snobj_err(EINVAL, "'masks' must be a list of blobs");

	if (masks->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d masks", 
				priv->num_fields);

	for (int i = 0; i < fields->size; i++) {
		struct snobj *field_val = snobj_list_get(arg, i);
		struct snobj *mask_val = snobj_list_get(arg, i);
		uint64_t *p1;
		uint64_t *p2;

		if (snobj_type(field_val) != TYPE_BLOB ||
				field_val->size != priv->fields[i].size)
			return snobj_err(EINVAL, 
					"field %d must be BLOB of %d bytes",
					i, priv->fields[i].size);

		if (snobj_type(mask_val) != TYPE_BLOB ||
				mask_val->size != priv->fields[i].size)
			return snobj_err(EINVAL, 
					"mask %d must be BLOB of %d bytes",
					i, priv->fields[i].size);

		p1 = snobj_blob_get(field_val);
		p2 = snobj_blob_get(mask_val);
		*(uint64_t *)(key + priv->fields[i].size_acc) = *p1;
		*(uint64_t *)(mask + priv->fields[i].size_acc) = *p2;
	}

	idx = find_entry(priv, key, mask);
	if (idx < 0)
		return snobj_err(ENOENT, "the rule does not exist");

	priv->num_rules--;
	memmove(&priv->rules[idx], &priv->rules[idx + 1],
			sizeof(struct rule) * (priv->num_rules - idx));

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
