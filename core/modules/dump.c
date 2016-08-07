#include <rte_hexdump.h>

#include "../module.h"

#define NS_PER_SEC	1000000000ul

static const uint64_t default_interval_ns = 1 * NS_PER_SEC;	/* 1 sec */

struct dump_priv {
	uint64_t min_interval_ns;
	uint64_t next_ns;
};

static struct snobj *
command_set_interval(struct module *m, const char *cmd, struct snobj *arg)
{
	struct dump_priv *priv = get_priv(m);

	double sec = snobj_number_get(arg);

	if (isnan(sec) || sec < 0.0)
		return snobj_err(EINVAL, "invalid interval");

	priv->min_interval_ns = (uint64_t)(sec * NS_PER_SEC);

	return NULL;
}

static struct snobj *dump_init(struct module *m, struct snobj *arg)
{
	struct dump_priv *priv = get_priv(m);

	priv->min_interval_ns = default_interval_ns;
	priv->next_ns = ctx.current_tsc;

	if (arg && (arg = snobj_eval(arg, "interval")))
		return command_set_interval(m, NULL, arg);
	else
		return NULL;
}

static void dump_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct dump_priv *priv = get_priv(m);

	if (unlikely(ctx.current_ns >= priv->next_ns)) {
		struct snbuf *pkt = batch->pkts[0];

		printf("----------------------------------------\n");
		printf("%s: packet dump\n", m->name);
		snb_dump(stdout, pkt);
		rte_hexdump(stdout, "Metadata buffer",
				pkt->_metadata, SNBUF_METADATA);
		priv->next_ns = ctx.current_ns + priv->min_interval_ns;
	}

	run_choose_module(m, get_igate(), batch);
}

static const struct mclass dump = {
	.name 		= "Dump",
	.help		= "Dump packet data and metadata attributes",
	.num_igates	= 1,
	.num_ogates	= 1,
	.priv_size	= sizeof(struct dump_priv),
	.init		= dump_init,
	.process_batch 	= dump_process_batch,
	.commands	= {
		{"set_interval", 	command_set_interval},
	}
};

ADD_MCLASS(dump)
