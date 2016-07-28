#include "../module.h"

struct generic_decap_priv {
	int decap_size;
};

static struct snobj *generic_decap_init(struct module *m, struct snobj *arg)
{
	struct generic_decap_priv *priv = get_priv(m);

	if (!arg)
		return NULL;

	if (snobj_type(arg) == TYPE_INT)
		priv->decap_size = snobj_uint_get(arg);
	else if (snobj_type(arg) == TYPE_MAP && snobj_eval_exists(arg, "bytes"))
		priv->decap_size = snobj_eval_uint(arg, "bytes");
	else
		return snobj_err(EINVAL, "invalid argument");

	if (priv->decap_size <= 0 || priv->decap_size > 1024)
		return snobj_err(EINVAL, "invalid decap size");

	return NULL;
}

static void
generic_decap_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct generic_decap_priv *priv = get_priv(m);
	int cnt = batch->cnt;

	int decap_size = priv->decap_size;

	for (int i = 0; i < cnt; i++)
		snb_adj(batch->pkts[i], decap_size);

	run_next_module(m, batch);
}

static const struct mclass generic_decap = {
	.name			= "GenericDecap",
	.help			= "remove specified bytes from the beginning "
				  "of packets",
	.def_module_name	= "generic_decap",
	.num_igates		= 1,
	.num_ogates		= 1,
	.priv_size		= sizeof(struct generic_decap_priv),
	.init 			= generic_decap_init,
	.process_batch		= generic_decap_process_batch,
};

ADD_MCLASS(generic_decap)
