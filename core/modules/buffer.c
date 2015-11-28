#include "../module.h"

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

	for (int i = 0; i < batch->cnt; i++) {
		batch_add(buf, batch->pkts[i]);

		if (batch_full(buf)) {
			run_next_module(m, buf);
			batch_clear(buf);
		}
	}
}

static const struct mclass buffer = {
	.name		= "Buffer",
	.priv_size 	= sizeof(struct buffer_priv),
	.deinit		= buffer_deinit,
	.process_batch  = buffer_process_batch,
};

ADD_MCLASS(buffer)
