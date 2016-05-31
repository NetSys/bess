#include "../module.h"

#include <rte_errno.h>
#include <rte_hash.h>
#include <rte_jhash.h>

#define DEFAULT_TABLE_SIZE	1024
#define MAX_FIELDS		8
#define USE_BULK_LOOKUP		0	

/* bulk version is actually much slower, if the hash table fits in the cache */
#if USE_BULK_LOOKUP
  #define BULK_THRESHOLD		8
#else
  #define BULK_THRESHOLD		9999
#endif

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

struct em_priv {
	struct rte_hash *tbl;
	int tbl_size;

	gate_idx_t default_gate;

	int num_fields;
	struct field {
		/* bits with 1: the bit must be considered.
		 * bits with 0: don't care */
		uint64_t mask;

		/* Offset in the packet data.
		 * For attribute-based fields, the value would be negative.
		 * For offset-based fields, the value is relative.
		 *  (starts from data_off, not the beginning of the headroom */
		int16_t offset;

		int attr_id;	/* -1 for offset-based fields */

		uint8_t size_acc;

		uint8_t size;	/* in bytes. 1 <= size <= MAX_FIELD_SIZE */
	} fields[MAX_FIELDS];

	uint32_t total_field_size;
};

static struct snobj *
add_field_one(struct module *m, struct snobj *field, struct field *f)
{
	if (field->type != TYPE_MAP)
		return snobj_err(EINVAL, 
				"'fields' must be a list of maps");

	f->size = snobj_eval_uint(field, "size");
	f->mask = snobj_eval_uint(field, "mask");

	if (f->size < 1 || f->size > MAX_FIELD_SIZE)
		return snobj_err(EINVAL, "'size' must be 1-%d",
				MAX_FIELD_SIZE);

	/* null mask doesn't make any sense... */
	if (f->mask == 0)
		f->mask = ~0ul;

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

/* Takes a list of fields. Each field needs 'offset' and 'size', 
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
	priv->total_field_size = size_acc;
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

	priv->tbl = rte_hash_create(&params);
	if (!priv->tbl)
		return snobj_err(rte_errno, "rte_hash_create() failed");

	return NULL;
}

static void em_deinit(struct module *m)
{
	struct em_priv *priv = get_priv(m);

	rte_hash_free(priv->tbl);
}

static void em_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct em_priv *priv = get_priv(m);

	gate_idx_t default_gate;
	gate_idx_t ogates[MAX_PKT_BURST];

	char keys[MAX_PKT_BURST][HASH_KEY_SIZE];

	int cnt = batch->cnt;

	default_gate = ACCESS_ONCE(priv->default_gate);

	/* collect keys 
	 * (optimization TODO: we can skip this if only one field is used) */
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

	if (cnt >= BULK_THRESHOLD) {
		uintptr_t results[MAX_PKT_BURST];
		uint64_t hits;

		char *key_ptrs[MAX_PKT_BURST];

		for (int i = 0; i < cnt; i++)
			key_ptrs[i] = keys[i];

		rte_hash_lookup_bulk_data(priv->tbl, (const void **)key_ptrs, 
				cnt, &hits, (void **)results);
	
		/* branchless version didn't help */
		for (int i = 0; i < cnt; i++) {
			if (hits & (1 << i))
				ogates[i] = results[i];
			else
				ogates[i] = default_gate;
		}
	} else {
		struct rte_hash *tbl = priv->tbl;
		int key_size = priv->total_field_size;

		for (int i = 0; i < cnt; i++) {
			uintptr_t result = default_gate;
		
			/* exploit that result is not updated if unmatched */
			rte_hash_lookup_with_hash_data(tbl,
					(const void *)keys[i], 
					DEFAULT_HASH_FUNC(keys[i], key_size, 0),
					(void **)&result);

			ogates[i] = result;
		}
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
		struct snobj *field_val = snobj_list_get(fields, i);
		uint64_t *p;

		if (snobj_type(field_val) != TYPE_BLOB ||
				field_val->size != priv->fields[i].size)
			return snobj_err(EINVAL, 
					"field %d must be BLOB of %d bytes",
					i, priv->fields[i].size);

		p = snobj_blob_get(field_val);
		*(uint64_t *)(key + priv->fields[i].size_acc) = *p;
	}

	ret = rte_hash_add_key_data(priv->tbl, key, (void *)(uintptr_t)gate);
	if (ret)
		return snobj_err(-ret, "rte_hash_add_key_data() failed");

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
		return snobj_err(EINVAL, "must specify %d arg", 
				priv->num_fields);

	for (int i = 0; i < arg->size; i++) {
		struct snobj *field_val = snobj_list_get(arg, i);
		uint64_t *p;

		if (snobj_type(field_val) != TYPE_BLOB ||
				field_val->size != priv->fields[i].size)
			return snobj_err(EINVAL, 
					"field %d must be BLOB of %d bytes",
					i, priv->fields[i].size);

		p = snobj_blob_get(field_val);
		*(uint64_t *)(key + priv->fields[i].size_acc) = *p;
	}

	ret = rte_hash_del_key(priv->tbl, key);
	if (ret < 0)
		return snobj_err(-ret, "rte_hash_del_key() failed");

	return NULL;
}

static struct snobj *
command_clear(struct module *m, const char *cmd, struct snobj *arg)
{
	struct em_priv *priv = get_priv(m);

	rte_hash_reset(priv->tbl);

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
