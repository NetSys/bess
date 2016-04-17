#include <rte_tcp.h>

#include "../module.h"
#include "../utils/histogram.h"

static inline void
timestamp_packet(struct snbuf* pkt, uint64_t time)
{
	uint8_t *avail = (uint8_t*)((uint8_t*)snb_head_data(pkt) +
			sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr)) +
		sizeof(struct tcp_hdr);
	*avail = 1;
	uint64_t *ts = (uint64_t*)(avail + 1);
	*ts = time;
}

static void
timestamp_process_batch(struct module *m, struct pkt_batch *batch)
{
	uint64_t time = get_time();

	for (int i = 0; i < batch->cnt; i++)
		timestamp_packet(batch->pkts[i], time);

	run_next_module(m, batch);
}

static const struct mclass timestamp = {
	.name 		= "Timestamp",
	.help		= 
		"marks current time to packets (paired with Measure module)",
	.num_igates 	= 1,
	.num_ogates	= 1,
	.process_batch 	= timestamp_process_batch,
};

ADD_MCLASS(timestamp)
