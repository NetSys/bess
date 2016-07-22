#include "../module.h"

struct source_priv {
	int pkt_size;
	int burst;
};

static struct snobj *
command_set_pkt_size(struct module *m, const char *cmd, struct snobj *arg);

static struct snobj *
command_set_burst(struct module *m, const char *cmd, struct snobj *arg);

static struct snobj *source_init(struct module *m, struct snobj *arg)
{
	struct source_priv *priv = get_priv(m);

	task_id_t tid;

	struct snobj *t;
	struct snobj *err;

	tid = register_task(m, NULL);
	if (tid == INVALID_TASK_ID)
		return snobj_err(ENOMEM, "Task creation failed");

	priv->pkt_size = 60; 	/* min-sized Ethernet frames */
	priv->burst = MAX_PKT_BURST;

	if (!arg)
		return NULL;

	if ((t = snobj_eval(arg, "pkt_size")) != NULL) {
		err = command_set_pkt_size(m, NULL, t);
		if (err)
			return err;
	}

	if ((t = snobj_eval(arg, "burst")) != NULL) {
		err = command_set_burst(m, NULL, t);
		if (err)
			return err;
	}

	return NULL;
}

static struct snobj *
command_set_pkt_size(struct module *m, const char *cmd, struct snobj *arg)
{
	struct source_priv *priv = get_priv(m);
	uint64_t val;

	if (snobj_type(arg) != TYPE_INT)
		return snobj_err(EINVAL, "pkt_size must be an integer");

	val = snobj_uint_get(arg);

	if (val == 0 || val > SNBUF_DATA)
		return snobj_err(EINVAL, "Invalid packet size");

	priv->pkt_size = val;

	return NULL;
}

static struct snobj *
command_set_burst(struct module *m, const char *cmd, struct snobj *arg)
{
	struct source_priv *priv = get_priv(m);
	uint64_t val;

	if (snobj_type(arg) != TYPE_INT)
		return snobj_err(EINVAL, "burst must be an integer");

	val = snobj_uint_get(arg);

	if (val == 0 || val > MAX_PKT_BURST)
		return snobj_err(EINVAL, "burst size must be [1,%d]",
				MAX_PKT_BURST);

	priv->burst = val;

	return NULL;
}

static struct task_result
source_run_task(struct module *m, void *arg)
{
	struct source_priv *priv = get_priv(m);

	struct pkt_batch batch;
	struct task_result ret;

	const int pkt_overhead = 24;

	const int pkt_size = ACCESS_ONCE(priv->pkt_size);
	const int burst = ACCESS_ONCE(priv->burst);

	uint64_t total_bytes = pkt_size * burst;

	int cnt = snb_alloc_bulk(batch.pkts, burst, pkt_size);

	if (cnt > 0) {
		batch.cnt = cnt;
		run_next_module(m, &batch);
	}

	ret = (struct task_result) {
		.packets = cnt,
		.bits = (total_bytes + cnt * pkt_overhead) * 8,
	};

	return ret;
}

static const struct mclass source = {
	.name 		= "Source",
	.help		=
		"infinitely generates packets with uninitialized data",
	.num_igates 	= 0,
	.num_ogates	= 1,
	.priv_size	= sizeof(struct source_priv),
	.init 		= source_init,
	.run_task 	= source_run_task,
	.commands	= {
		{"set_pkt_size", command_set_pkt_size, .mt_safe=1},
		{"set_burst", command_set_burst, .mt_safe=1},
	}
};

ADD_MCLASS(source)
