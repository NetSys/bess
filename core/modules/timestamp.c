#include <rte_tcp.h>

#include "../module.h"
#include "../utils/histogram.h"

/* XXX: currently doesn't support multiple workers */
struct timestamp_priv {
	uint64_t start_time;
	int64_t warmup;
	uint64_t out_pkt_cnt;
	uint64_t out_bytes_cnt;
};

static struct snobj *timestamp_init(struct module *m, struct snobj *arg)
{
	struct timestamp_priv *priv = get_priv(m);

	if (arg)
		priv->warmup = snobj_eval_int(arg, "warmup");

	return NULL;
}

static struct snobj *timestamp_query(struct module *m, struct snobj *q)
{
	struct timestamp_priv *priv = get_priv(m);

	struct snobj *r = snobj_map();

	uint64_t pkt_total = 0;
	uint64_t byte_total = 0;

	pkt_total += priv->out_pkt_cnt;
	byte_total += priv->out_bytes_cnt;

	snobj_map_set(r, "packets", snobj_int(pkt_total));
	snobj_map_set(r, "bytes", snobj_int(byte_total));

	return r;
}

static inline void
timestamp_packet(struct snbuf* pkt, uint64_t time, int account_for_packet)
{
	uint8_t *avail = (uint8_t*)((uint8_t*)snb_head_data(pkt) +
			sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr)) +
		sizeof(struct tcp_hdr);
	*avail = account_for_packet;
	uint64_t *ts = (uint64_t*)(avail + 1);
	*ts = time;
}

static void
timestamp_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct timestamp_priv *priv = get_priv(m);

	int account_for_packet = 0;
	uint64_t time = get_time();
	int i = 0;

	if (priv->start_time == 0)
		priv->start_time = time;

	if (time - priv->start_time > 
			priv->warmup * (HISTO_TIMEUNIT_MULT / HISTO_TIME)) 
	{
		priv->out_pkt_cnt += batch->cnt;
		account_for_packet = 1;

		for (i = 0; i < batch->cnt; i++)
			priv->out_bytes_cnt += batch->pkts[i]->mbuf.pkt_len;
	}

	for (i = 0; i < batch->cnt; i++)
		timestamp_packet(batch->pkts[i], time, account_for_packet);

	run_next_module(m, batch);
}

static const struct mclass timestamp = {
	.name 		= "Timestamp",
	.num_igates 	= 1,
	.num_ogates	= 1,
	.priv_size	= sizeof(struct timestamp_priv),
	.init 		= timestamp_init,
	.process_batch 	= timestamp_process_batch,
	.query		= timestamp_query,
};

ADD_MCLASS(timestamp)
