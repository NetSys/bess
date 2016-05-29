#include "../module.h"

#define MAX_FIELDS		16

struct update_priv {
	int num_fields;

	struct field {
		uint64_t mask;		/* bits with 1 won't be updated */
		uint64_t value;		/* in network order */
		int16_t offset;
	} fields[MAX_FIELDS];
};

static struct snobj *
command_add(struct module *m, const char *cmd, struct snobj *arg)
{
	struct update_priv *priv = get_priv(m);

	int curr = priv->num_fields;

	if (snobj_type(arg) != TYPE_LIST)
		return snobj_err(EINVAL, "argument must be a list of maps");

	if (curr + arg->size > MAX_FIELDS)
		return snobj_err(EINVAL, "max %d variables " \
				"can be specified", MAX_FIELDS);

	for (int i = 0; i < arg->size; i++) {
		struct snobj *field = snobj_list_get(arg, i);

		uint8_t size;
		int16_t offset;
		uint64_t mask;
		uint64_t value;

		const char *value_str;

		if (field->type != TYPE_MAP)
			return snobj_err(EINVAL, 
					"argument must be a list of maps");

		offset = snobj_eval_int(field, "offset");
		size = snobj_eval_uint(field, "size");

		value_str = snobj_eval_str(field, "value");
		if (value_str) {
			int ret = sscanf(value_str, "%lx", &value);
			if (ret < 1)
				return snobj_err(EINVAL, 
						"not a valid hex number '%s'",
						value_str);
		} else
			value = snobj_eval_uint(field, "value");

		if (offset < 0)
			return snobj_err(EINVAL, "too small 'offset'");

		if (size < 1 || size > 8)
			return snobj_err(EINVAL, "'size' must be 1-8");

		offset -= (8 - size);
		mask = (1ul << ((8 - size) * 8)) - 1;

		if (offset + 8 > SNBUF_DATA)
			return snobj_err(EINVAL, "too large 'offset'");

		priv->fields[curr + i].offset = offset;
		priv->fields[curr + i].mask = mask;
		priv->fields[curr + i].value = rte_cpu_to_be_64(value);
	}

	priv->num_fields = curr + arg->size;

	return NULL;
}

static struct snobj *
command_clear(struct module *m, const char *cmd, struct snobj *arg)
{
	struct update_priv *priv = get_priv(m);

	priv->num_fields = 0;

	return NULL;
}

static struct snobj *update_init(struct module *m, struct snobj *arg)
{
	if (arg)
		return command_add(m, NULL, arg);
	else
		return NULL;
}

static void update_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct update_priv *priv = get_priv(m);

	int cnt = batch->cnt;

	for (int i = 0; i < priv->num_fields; i++) {
		const struct field *field = &priv->fields[i];

		uint64_t mask = field->mask;
		uint64_t value = field->value;
		int16_t offset = field->offset;
			
		for (int j = 0; j < cnt; j++) {
			struct snbuf *snb = batch->pkts[j];
			char *head = snb_head_data(snb);

			uint64_t * restrict p = (uint64_t *)(head + offset);
			
			*p = (*p & mask) | value;
		}
	}

	run_next_module(m, batch);
}

static const struct mclass update = {
	.name 			= "Update",
	.help			= "updates packet data with specified values",
	.def_module_name	= "update",
	.num_igates		= 1,
	.num_ogates		= 1,
	.priv_size		= sizeof(struct update_priv),
	.init 			= update_init,
	.process_batch 		= update_process_batch,
	.commands		= {
		{"add", 	command_add},
		{"clear", 	command_clear},
	}
};

ADD_MCLASS(update)
