#include "../module.h"
#include "../utils/random.h"

#define MAX_VARS		16

struct rupdate_priv {
	int num_vars;
	struct var {
		uint32_t mask;
		uint32_t min;
		uint32_t range;		/* == max - min + 1 */
		int16_t offset;
	} vars[MAX_VARS];

	uint64_t seed;
};

static struct snobj *rupdate_query(struct module *, struct snobj *);

static struct snobj *rupdate_init(struct module *m, struct snobj *arg)
{
	struct rupdate_priv *priv = get_priv(m);

	priv->seed = 1;

	if (arg)
		return rupdate_query(m, arg);
	
	return NULL;
}

static struct snobj *handle_vars(struct rupdate_priv *priv, 
		struct snobj *vars)
{
	if (snobj_type(vars) != TYPE_LIST)
		return snobj_err(EINVAL, "'vars' must be a list of maps");

	if (vars->size > MAX_VARS)
		return snobj_err(EINVAL, "Max %d variables " \
				"can be specified", MAX_VARS);

	priv->num_vars = 0;

	for (int i = 0; i < vars->size; i++) {
		struct snobj *var = snobj_list_get(vars, i);

		uint8_t size;
		int16_t offset;
		uint32_t mask;
		uint32_t min;
		uint32_t max;

		if (var->type != TYPE_MAP)
			return snobj_err(EINVAL, 
					"'vars' must be a list of maps");

		offset = snobj_eval_int(var, "offset");
		size = snobj_eval_uint(var, "size");
		min = snobj_eval_uint(var, "min");
		max = snobj_eval_uint(var, "max");

		if (offset < 0)
			return snobj_err(EINVAL, "Too small 'offset'");

		switch (size) {
		case 1:
			offset -= 3;
			mask = rte_cpu_to_be_32(0xffffff00);
			min = MIN(min, 0xff);
			max = MIN(max, 0xff);
			break;

		case 2:
			offset -= 2;
			mask = rte_cpu_to_be_32(0xffff0000);
			min = MIN(min, 0xffff);
			max = MIN(max, 0xffff);
			break;

		case 4:
			mask = rte_cpu_to_be_32(0x00000000);
			min = MIN(min, 0xffffffff);
			max = MIN(max, 0xffffffff);
			break;

		default:
			return snobj_err(EINVAL, "'size' must be 1, 2, or 4");
		}

		if (offset + 4 > SNBUF_DATA)
			return snobj_err(EINVAL, "Too large 'offset'");

		if (min > max)
			return snobj_err(EINVAL, "'min' should not be " \
					"greater than 'max'");

		priv->vars[i].offset = offset;
		priv->vars[i].mask = mask;
		priv->vars[i].min = min;

		/* avoid modulo 0 */
		priv->vars[i].range = (max - min + 1) ? : 0xffffffff;
	}

	priv->num_vars = vars->size;

	return NULL;
}

static struct snobj *rupdate_query(struct module *m, struct snobj *q)
{
	struct rupdate_priv *priv = get_priv(m);

	struct snobj *vars = snobj_eval(q, "vars");

	struct snobj *err;

	if (vars) {
		err = handle_vars(priv, vars);
		if (err)
			return err;
	}

	return NULL;
}

static void rupdate_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct rupdate_priv *priv = get_priv(m);

	uint64_t seed = priv->seed;
	int cnt = batch->cnt;

	for (int i = 0; i < priv->num_vars; i++) {
		const struct var *var = &priv->vars[i];

		uint32_t mask = var->mask;
		uint32_t min = var->min;
		uint32_t range = var->range;
		int16_t offset = var->offset;
			
		for (int j = 0; j < cnt; j++) {
			struct snbuf *snb = batch->pkts[j];
			char *head = snb_head_data(snb);

			uint32_t * restrict p;
			uint32_t rand_val;

			p = (uint32_t *)(head + offset);
			rand_val = min + rand_fast_range(&seed, range);
			
			*p = (*p & mask) | rte_cpu_to_be_32(rand_val);
		}
	}

	priv->seed = seed;

	run_next_module(m, batch);
}

static const struct mclass rupdate = {
	.name 			= "RandomUpdate",
	.def_module_name	= "rupdate",
	.num_igates		= 1,
	.num_ogates		= 1,
	.priv_size		= sizeof(struct rupdate_priv),
	.init 			= rupdate_init,
	.query			= rupdate_query,
	.process_batch 		= rupdate_process_batch,
};

ADD_MCLASS(rupdate)
