#include "../utils/htable.h"

#include "../module.h"

#define MAX_FIELDS		8
#define MAX_FIELD_SIZE		8
ct_assert(MAX_FIELD_SIZE <= sizeof(uint64_t));

#define HASH_KEY_SIZE		(MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
  #error this code assumes little endian architecture (x86)
#endif

typedef char hkey_t[HASH_KEY_SIZE];

HT_DECLARE_INLINED_FUNCS(em, hkey_t)

struct em_priv {
	gate_idx_t default_gate;

	uint32_t total_key_size;

	int num_fields;
	struct field {
		/* bits with 1: the bit must be considered.
		 * bits with 0: don't care */
		uint64_t mask;

		int attr_id;	/* -1 for offset-based fields */

		/* Relative offset in the packet data for offset-based fields.
		 *  (starts from data_off, not the beginning of the headroom */
		int16_t offset;

		uint8_t size_acc;

		uint8_t size;	/* in bytes. 1 <= size <= MAX_FIELD_SIZE */
	} fields[MAX_FIELDS];

	struct htable ht;
};

static inline int
em_keycmp(const hkey_t *key, const hkey_t *key_stored, size_t key_len)
{
	return memcmp(key, key_stored, key_len);
}

static inline uint32_t
em_hash(const hkey_t *key, uint32_t key_len, uint32_t init_val)
{
	return rte_hash_crc(key, key_len, init_val);
}

static struct snobj *
add_field_one(struct module *m, struct snobj *field, struct field *f, int idx)
{
	if (field->type != TYPE_MAP)
		return snobj_err(EINVAL, 
				"'fields' must be a list of maps");

	f->size = snobj_eval_uint(field, "size");
	if (f->size < 1 || f->size > MAX_FIELD_SIZE)
		return snobj_err(EINVAL, "idx %d: 'size' must be 1-%d",
				idx, MAX_FIELD_SIZE);

	const char *attr_name = snobj_eval_str(field, "name");

	if (attr_name) {
		f->attr_id = add_metadata_attr(m, attr_name, f->size, MT_READ);
		if (f->attr_id < 0)
			return snobj_err(-f->attr_id, 
					"idx %d: add_metadata_attr() failed",
					idx);
	} else if (snobj_eval_exists(field, "offset")) {
		f->attr_id = -1;
		f->offset = snobj_eval_int(field, "offset");
		if (f->offset < 0 || f->offset > 1024)
			return snobj_err(EINVAL, "idx %d: invalid 'offset'",
					idx);
	}  else
		return snobj_err(EINVAL, 
				"idx %d: must specify 'offset' or 'name'", idx);

	struct snobj *mask = snobj_eval(field, "mask");
	int force_be = (f->attr_id < 0);

	if (!mask) {
		/* by default all bits are considered */
		f->mask = (1ul << (f->size * 8)) - 1;
	} else if (snobj_binvalue_get(mask, f->size, &f->mask, force_be))
		return snobj_err(EINVAL, "idx %d: not a correct %d-byte mask", 
				idx, f->size);

	if (f->mask == 0)
		return snobj_err(EINVAL, "idx %d: empty mask", idx);

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
static struct snobj *em_init(struct module *m, struct snobj *arg)
{
	struct em_priv *priv = get_priv(m);
	int8_t size_acc = 0;

	struct snobj *fields = snobj_eval(arg, "fields");

	if (snobj_type(fields) != TYPE_LIST)
		return snobj_err(EINVAL, "'fields' must be a list of maps");

	for (int i = 0; i < fields->size; i++) {
		struct snobj *field = snobj_list_get(fields, i);
		struct snobj *err;
		struct field f;

		f.size_acc = size_acc;

		err = add_field_one(m, field, &f, i);
		if (err)
			return err;

		size_acc += f.size;
		priv->fields[i] = f;
	}

	priv->default_gate = DROP_GATE;
	priv->num_fields = fields->size;
	priv->total_key_size = size_acc;

	int ret = ht_init(&priv->ht, size_acc, sizeof(gate_idx_t));
	if (ret < 0) 
		return snobj_err(-ret, "hash table creation failed");

	return NULL;
}

static void em_deinit(struct module *m)
{
	struct em_priv *priv = get_priv(m);

	ht_close(&priv->ht);
}

static void em_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct em_priv *priv = get_priv(m);

	gate_idx_t default_gate;
	gate_idx_t ogates[MAX_PKT_BURST];

	char keys[MAX_PKT_BURST][HASH_KEY_SIZE] __ymm_aligned;

	int cnt = batch->cnt;

	default_gate = ACCESS_ONCE(priv->default_gate);

	for (int i = 0; i < priv->num_fields; i++) {
		uint64_t mask = priv->fields[i].mask;
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

			*(uint64_t *)key = 
				*(uint64_t *)(buf_addr + offset) & mask;
		}
	}

	const struct htable *t = &priv->ht;

	for (int i = 0; i < cnt; i++) {
		gate_idx_t *ret = ht_em_get(t, keys[i]);
		ogates[i] = ret ? *ret : default_gate;
	}

	run_split(m, ogates, batch);
}

static struct snobj *
command_add(struct module *m, const char *cmd, struct snobj *arg)
{
	struct em_priv *priv = get_priv(m);

	struct snobj *fields = snobj_eval(arg, "fields");
	gate_idx_t gate = snobj_eval_uint(arg, "gate");

	char key[HASH_KEY_SIZE];

	int ret;

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

	for (int i = 0; i < fields->size; i++) {
		int field_size = priv->fields[i].size;
		int field_size_acc = priv->fields[i].size_acc;

		struct snobj *f_obj = snobj_list_get(fields, i);
		uint64_t f;

		int force_be = (priv->fields[i].attr_id < 0);

		if (snobj_binvalue_get(f_obj, field_size, &f, force_be))
			return snobj_err(EINVAL,
					"idx %d: not a correct %d-byte value",
					i, field_size);

		memcpy(key + field_size_acc, &f, field_size);
	}

	ret = ht_set(&priv->ht, key, &gate);
	if (ret)
		return snobj_err(-ret, "ht_set() failed");

	return NULL;
}

static struct snobj *
command_delete(struct module *m, const char *cmd, struct snobj *arg)
{
	struct em_priv *priv = get_priv(m);

	char key[HASH_KEY_SIZE];

	int ret;

	if (!arg || snobj_type(arg) != TYPE_LIST)
		return snobj_err(EINVAL, "argument must be a list of blobs");

	if (arg->size != priv->num_fields)
		return snobj_err(EINVAL, "must specify %d fields", 
				priv->num_fields);

	for (int i = 0; i < arg->size; i++) {
		int field_size = priv->fields[i].size;
		int field_size_acc = priv->fields[i].size_acc;

		struct snobj *f_obj = snobj_list_get(arg, i);
		uint64_t f;

		int force_be = (priv->fields[i].attr_id < 0);

		if (snobj_binvalue_get(f_obj, field_size, &f, force_be))
			return snobj_err(EINVAL,
					"idx %d: not a correct %d-byte value",
					i, field_size);

		memcpy(key + field_size_acc, &f, field_size);
	}

	ret = ht_del(&priv->ht, key);
	if (ret < 0)
		return snobj_err(-ret, "ht_del() failed");

	return NULL;
}

static struct snobj *
command_clear(struct module *m, const char *cmd, struct snobj *arg)
{
	struct em_priv *priv = get_priv(m);
	struct htable *t = &priv->ht;
	uint32_t next = 0;
	void *key;

	while ((key = ht_iterate(t, &next)))
		ht_del(t, key);

	return NULL;
}

static struct snobj *
command_set_default_gate(struct module *m, const char *cmd, struct snobj *arg)
{
	struct em_priv *priv = get_priv(m);

	int gate = snobj_int_get(arg);

	priv->default_gate = gate;

	return NULL;
}

static const struct mclass em = {
	.name 			= "ExactMatch",
	.help			= 
		"Multi-field classifier with a exact match table",
	.def_module_name	= "em",
	.num_igates		= 1,
	.num_ogates		= MAX_GATES,
	.priv_size		= sizeof(struct em_priv),
	.init 			= em_init,
	.deinit          	= em_deinit,
	.process_batch 		= em_process_batch,
	.commands		= {
		{"add", 		command_add},
		{"delete", 		command_delete},
		{"clear", 		command_clear},
		{"set_default_gate",	command_set_default_gate, .mt_safe=1},
	}
};

ADD_MCLASS(em)
