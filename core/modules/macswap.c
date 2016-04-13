#include "../module.h"

static void macswap_process_batch(struct module *m, struct pkt_batch *batch)
{
	int cnt = batch->cnt;

	for (int i = 0; i < cnt; i++) {
		char *head = snb_head_data(batch->pkts[i]);
		char tmp[6];

		rte_memcpy(tmp, head, 6);
		rte_memcpy(head, head + 6, 6);
		rte_memcpy(head + 6, tmp, 6);
	}

	run_next_module(m, batch);
}

static const struct mclass macswap = {
	.name 			= "MACSwap",
	.def_module_name	= "macswap",
	.num_igates		= 1,
	.num_ogates		= 1,
	.process_batch 		= macswap_process_batch,
};

ADD_MCLASS(macswap)
