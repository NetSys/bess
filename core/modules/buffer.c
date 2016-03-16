#include "../module.h"

/* TODO: timer-triggered flush */
/* TODO: use per-task stroage */
struct buffer_priv {
	struct pkt_batch buf;
};

static void buffer_deinit(struct module *m)
{
	struct buffer_priv *priv = get_priv(m);
	struct pkt_batch *buf = &priv->buf;

	if (buf->cnt)
		snb_free_bulk(buf->pkts, buf->cnt);
}

static void buffer_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct buffer_priv *priv = get_priv(m);
	struct pkt_batch *buf = &priv->buf;

	int free_slots = MAX_PKT_BURST - buf->cnt;
	int left = batch->cnt;

	snb_array_t p_buf = &buf->pkts[buf->cnt];
	snb_array_t p_batch = &batch->pkts[0];

	if (left >= free_slots) {
		buf->cnt = MAX_PKT_BURST;
		rte_memcpy((void *)p_buf, (void *)p_batch, 
				free_slots * sizeof(struct snbuf *));

		p_buf = &buf->pkts[0];
		p_batch += free_slots;
		left -= free_slots;

		run_next_module(m, buf);
		batch_clear(buf);
	}

	buf->cnt += left;
	rte_memcpy((void *)p_buf, (void *)p_batch, 
			left * sizeof(struct snbuf *));
}

static const struct mclass buffer = {
	.name		= "Buffer",
	.num_igates	= 1,
	.num_ogates	= 1,
	.priv_size 	= sizeof(struct buffer_priv),
	.deinit		= buffer_deinit,
	.process_batch  = buffer_process_batch,
};

ADD_MCLASS(buffer)
