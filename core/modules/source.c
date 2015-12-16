#include "../module.h"

struct source_priv {
	int pkt_size;
};

static struct snobj *source_query(struct module *, struct snobj *);

static struct snobj *source_init(struct module *m, struct snobj *arg)
{
	struct source_priv *priv = get_priv(m);
	task_id_t tid;

	priv->pkt_size = 60;	/* default: min-sized Ethernet frames */

	tid = register_task(m, NULL);
	if (tid == INVALID_TASK_ID)
		return snobj_err(ENOMEM, "Task creation failed");

	if (arg)
		return source_query(m, arg);
	
	return NULL;
}

static struct snobj *handle_pkt_size(struct source_priv *priv, 
		struct snobj *pkt_size)
{
	uint64_t val;
	
	if (snobj_type(pkt_size) != TYPE_INT)
		return snobj_err(EINVAL, 
				"'pkt_size' must be an integer");

	val = snobj_uint_get(pkt_size);

	if (val == 0 || val > SNBUF_DATA)
		return snobj_err(EINVAL, "Invalid packet size");

	priv->pkt_size = val;

	return NULL;
}

static struct snobj *source_query(struct module *m, struct snobj *q)
{
	struct source_priv *priv = get_priv(m);

	struct snobj *pkt_size = snobj_eval(q, "pkt_size");
	struct snobj *err;

	if (pkt_size) {
		err = handle_pkt_size(priv, pkt_size);
		if (err)
			return err;
	}

	return NULL;
}

static struct task_result
source_run_task(struct module *m, void *arg)
{
	struct source_priv *priv = get_priv(m);

	struct pkt_batch batch;
	struct task_result ret;

	const int pkt_overhead = 24;

	uint64_t total_bytes = priv->pkt_size * MAX_PKT_BURST;

	const int cnt = snb_alloc_bulk(batch.pkts, MAX_PKT_BURST, 
			priv->pkt_size);

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
	.priv_size	= sizeof(struct source_priv),
	.init 		= source_init,
	.query		= source_query,
	.run_task 	= source_run_task,
};

ADD_MCLASS(source)
