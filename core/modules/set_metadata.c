#include "../module.h"

#define MAX_ATTRS	MAX_ATTRS_PER_MODULE

typedef struct {
	char bytes[MT_ATTR_MAX_SIZE];
} value_t;

struct set_metadata_priv {
	int num_attrs;

	struct attr {
		char name[MT_ATTR_NAME_LEN];
		value_t value;
		int offset;
		int size;
	} attrs[MAX_ATTRS];
};

static struct snobj *add_attr_one(struct module *m, struct snobj *attr)
{
	struct set_metadata_priv *priv = get_priv(m);

	const char *name;
	int size = 0;
	int offset = -1;
	value_t value = {};

	struct snobj *t;

	int ret;

	if (priv->num_attrs >= MAX_ATTRS)
		return snobj_err(EINVAL, "max %d attributes " \
				"can be specified", MAX_ATTRS);

	if (attr->type != TYPE_MAP)
		return snobj_err(EINVAL,
				"argument must be a map or a list of maps");

	name = snobj_eval_str(attr, "name");
	if (!name)
		return snobj_err(EINVAL, "'name' field is missing");

	size = snobj_eval_uint(attr, "size");

	if (size < 1 || size > MT_ATTR_MAX_SIZE)
		return snobj_err(EINVAL, "'size' must be 1-%d",
				MT_ATTR_MAX_SIZE);

	if ((t = snobj_eval(attr, "value"))) {
		if (snobj_binvalue_get(t, size, &value, 0))
			return snobj_err(EINVAL, "'value' field has not a "
					"correct %d-byte value", size);
	} else if ((t = snobj_eval(attr, "offset"))) {
		if (snobj_type(t) != TYPE_INT)
			return snobj_err(EINVAL, "'offset' must be an integer");

		offset = snobj_int_get(t);
		if (offset < 0 || offset + size >= SNBUF_DATA)
			return snobj_err(EINVAL, "invalid packet offset");
	}

	ret = add_metadata_attr(m, name, size, MT_WRITE);
	if (ret < 0)
		return snobj_err(-ret, "add_metadata_attr() failed");

	strcpy(priv->attrs[priv->num_attrs].name, name);
	priv->attrs[priv->num_attrs].size = size;
	priv->attrs[priv->num_attrs].offset = offset;
	priv->attrs[priv->num_attrs].value = value;
	priv->num_attrs++;

	return NULL;
}

static struct snobj *add_attr_many(struct module *m, struct snobj *list)
{
	if (snobj_type(list) != TYPE_LIST)
		return snobj_err(EINVAL,
				"argument must be a map or a list of maps");

	for (int i = 0; i < list->size; i++) {
		struct snobj *attr = snobj_list_get(list, i);
		struct snobj *err;

		err = add_attr_one(m, attr);
		if (err)
			return err;
	}

	return NULL;
}

static struct snobj *set_metadata_init(struct module *m, struct snobj *arg)
{
	if (arg && snobj_type(arg) == TYPE_MAP)
		return add_attr_one(m, arg);
	else if (arg && snobj_type(arg) == TYPE_LIST)
		return add_attr_many(m, arg);
	else
		return snobj_err(EINVAL,
				"argument must be a map or a list of maps");
}

static void copy_from_packet(struct pkt_batch *batch,
		const struct attr *attr, mt_offset_t mt_off)
{
	int cnt = batch->cnt;
	int size = attr->size;

	int pkt_off = attr->offset;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *head = snb_head_data(pkt);
		void *mt_ptr;

		mt_ptr = _ptr_attr_with_offset(mt_off, pkt, value_t);
		rte_memcpy(mt_ptr, head + pkt_off, size);
	}
}

static void copy_from_value(struct pkt_batch *batch,
		const struct attr *attr, mt_offset_t mt_off)
{
	int cnt = batch->cnt;
	int size = attr->size;

	const void *val_ptr = &attr->value;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		void *mt_ptr;

		mt_ptr = _ptr_attr_with_offset(mt_off, pkt, value_t);
		rte_memcpy(mt_ptr, val_ptr, size);
	}
}

static void
set_metadata_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct set_metadata_priv *priv = get_priv(m);

	for (int i = 0; i < priv->num_attrs; i++) {
		const struct attr *attr = &priv->attrs[i];

		mt_offset_t mt_offset = mt_attr_offset(m, i);

		if (!is_valid_offset(mt_offset))
			continue;

		/* copy data from the packet */
		if (attr->offset >= 0)
			copy_from_packet(batch, attr, mt_offset);
		else
			copy_from_value(batch, attr, mt_offset);
	}

	run_next_module(m, batch);
}

static const struct mclass set_metadata = {
	.name 			= "SetMetadata",
	.def_module_name 	= "setattr",
	.help			= "Set metadata attributes to packets",
	.num_igates		= 1,
	.num_ogates		= 1,
	.priv_size		= sizeof(struct set_metadata_priv),
	.init 			= set_metadata_init,
	.process_batch		= set_metadata_process_batch,
};

ADD_MCLASS(set_metadata)
