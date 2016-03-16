#include <rte_tcp.h>
#include <string.h>

#include "../module.h"
#include "../utils/histogram.h"
#include "../time.h"

/* XXX: currently doesn't support multiple workers */
struct measure_priv {
	struct histogram hist;

	uint64_t start_time;
	int warmup;		/* second */

	uint64_t pkt_cnt;
	uint64_t bytes_cnt;
	uint64_t total_latency;
};

static struct snobj *measure_init(struct module *m, struct snobj *arg)
{
	struct measure_priv *priv = get_priv(m);

	if (arg)
		priv->warmup = snobj_eval_int(arg, "warmup");

	init_hist(&priv->hist);

	priv->start_time = get_time();

	return NULL;
}

struct snobj *measure_query(struct module *m, struct snobj *q)
{
	struct measure_priv *priv = get_priv(m);

	struct snobj *r = snobj_map();

	uint64_t pkt_total = priv->pkt_cnt;
	const char* query = snobj_eval_str(q, "type");

	if (!query)
		return snobj_err(ENOTSUP, "Missing 'type' field");

	snobj_map_set(r, "timestamp", snobj_double(get_epoch_time()));
	snobj_map_set(r, "packets", snobj_int(pkt_total));

	if (strcmp(query, "bw") == 0) {
		uint64_t byte_total = priv->bytes_cnt;
		uint64_t bits = (byte_total + pkt_total * 24) * 8;
		snobj_map_set(r, "bits", snobj_int(bits));
	} else if (strcmp(query, "latency") == 0) {
		snobj_map_set(r, "total_latency_ns", 
				snobj_int(priv->total_latency * 100ul));
	} else {
		snobj_free(r);
		return snobj_err(ENOTSUP, "Not supported query");
	}

	return r;
}

static inline int
get_measure_packet(struct snbuf* pkt, uint64_t* time)
{
	uint8_t *avail = (uint8_t*)((uint8_t*)snb_head_data(pkt) +
			sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr)) +
		sizeof(struct tcp_hdr);
	uint64_t *ts = (uint64_t*)(avail + 1);
	uint8_t available = *avail;
	*time = *ts;
	return available;
}

static void
measure_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct measure_priv *priv = get_priv(m);

	uint64_t time = get_time();
	int i = 0;

	if (time - priv->start_time >= priv->warmup) {
		priv->pkt_cnt += batch->cnt;

		for (i = 0; i < batch->cnt; i++) {
			uint64_t pkt_time;
			if (get_measure_packet(batch->pkts[i], &pkt_time)) {
				uint64_t diff;
				
				if (time >= pkt_time)
					diff = time - pkt_time;
				else
					continue;

				priv->bytes_cnt += batch->pkts[i]->mbuf.pkt_len;
				priv->total_latency += diff;

				record_latency(&priv->hist, diff);
			}
		}
	}

	run_next_module(m, batch);
}

static const struct mclass measure = {
	.name 		= "Measure",
	.num_igates	= 1,
	.num_ogates	= 1,
	.priv_size	= sizeof(struct measure_priv),
	.init 		= measure_init,
	.process_batch 	= measure_process_batch,
	.query		= measure_query,
};

ADD_MCLASS(measure)
