#include "../module.h"

#define MAX_FIELDS		8
#define MAX_FIELD_SIZE		8
ct_assert(MAX_FIELD_SIZE <= sizeof(uint64_t));

#define MAX_HEADER_SIZE		(MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
  #error this code assumes little endian architecture (x86)
#endif

struct generic_encap_priv {
	int encap_size;

	int num_fields;

	struct field {
		uint64_t value;	/* onlt for constant values */
		int attr_id;	/* -1 for constant values */
		int pos;	/* relative position in the new header */
		int size;	/* in bytes. 1 <= size <= MAX_FIELD_SIZE */
	} fields[MAX_FIELDS];
};

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

	struct snobj *t;

	if (attr_name) {
		f->attr_id = add_metadata_attr(m, attr_name, f->size, MT_READ);
		if (f->attr_id < 0)
			return snobj_err(-f->attr_id,
					"idx %d: add_metadata_attr() failed",
					idx);
	} else if ((t = snobj_eval(field, "value"))) {
		f->attr_id = -1;
		if (snobj_binvalue_get(t, f->size, &f->value, 1))
			return snobj_err(EINVAL, "idx %d: "
					"not a correct %d-byte value",
					idx, f->size);
	}  else
		return snobj_err(EINVAL,
				"idx %d: must specify 'value' or 'name'", idx);

	return NULL;
}

/* Takes a list of fields. Each field is either:
 *
 *  1. {'size': X, 'value': Y}		(for constant values)
 *  2. {'size': X, 'name': Y}		(for metadata attributes)
 *
 * e.g.: GenericEncap([{'size': 4, 'value':0xdeadbeef},
 *                     {'size': 2, 'name':'foo'},
 *                     {'size': 2, 'value':0x1234}])
 * will prepend a 8-byte header:
 *    de ad be ef <xx> <xx> 12 34
 * where the 2-byte <xx> <xx> comes from the value of metadata arribute 'foo'
 * for each packet.
 */
static struct snobj *generic_encap_init(struct module *m, struct snobj *arg)
{
	struct generic_encap_priv *priv = get_priv(m);
	int size_acc = 0;

	struct snobj *fields = snobj_eval(arg, "fields");

	if (snobj_type(fields) != TYPE_LIST)
		return snobj_err(EINVAL, "'fields' must be a list of maps");

	for (int i = 0; i < fields->size; i++) {
		struct snobj *field = snobj_list_get(fields, i);
		struct snobj *err;
		struct field *f = &priv->fields[i];

		f->pos = size_acc;

		err = add_field_one(m, field, f, i);
		if (err)
			return err;

		size_acc += f->size;
	}

	priv->encap_size = size_acc;
	priv->num_fields = fields->size;

	return NULL;
}

static void
generic_encap_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct generic_encap_priv *priv = get_priv(m);
	int cnt = batch->cnt;

	int encap_size = priv->encap_size;

	char headers[MAX_PKT_BURST][MAX_HEADER_SIZE] __ymm_aligned;

	for (int i = 0; i < priv->num_fields; i++) {
		uint64_t value = priv->fields[i].value;

		int attr_id = priv->fields[i].attr_id;
		int offset = (attr_id >= 0) ? mt_attr_offset(m, attr_id) : 0;

		char *header = headers[0] + priv->fields[i].pos;

		for (int j = 0; j < cnt; j++, header += MAX_HEADER_SIZE) {
			struct snbuf *pkt = batch->pkts[j];

			*(uint64_t *)header = (attr_id < 0) ?
				value :
				get_attr_with_offset(offset, pkt, uint64_t);
		}
	}

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];

		char *p = snb_prepend(pkt, encap_size);

		if (unlikely(!p))
			continue;

		rte_memcpy(p, headers[i], encap_size);
	}

	run_next_module(m, batch);
}

static const struct mclass generic_encap = {
	.name			= "GenericEncap",
	.help			= "encapsulates packets with constant values "
				  "and metadata attributes",
	.def_module_name	= "generic_encap",
	.num_igates		= 1,
	.num_ogates		= 1,
	.priv_size		= sizeof(struct generic_encap_priv),
	.init 			= generic_encap_init,
	.process_batch		= generic_encap_process_batch,
};

ADD_MCLASS(generic_encap)
