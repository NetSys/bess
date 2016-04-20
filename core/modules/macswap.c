#include "../module.h"

#include <rte_ether.h>

static void macswap_process_batch(struct module *m, struct pkt_batch *batch)
{
	int cnt = batch->cnt;

	for (int i = 0; i < cnt; i++) {
		char *head  = snb_head_data(batch->pkts[i]);
		struct ether_hdr *eth = (struct ether_hdr *)head;
		struct ether_addr tmp;

		tmp = eth->d_addr;
		eth->d_addr = eth->s_addr;
		eth->s_addr = tmp;
	}

	run_next_module(m, batch);
}

static const struct mclass macswap = {
	.name 			= "MACSwap",
	.help			= "swaps source/destination MAC addresses",
	.def_module_name	= "macswap",
	.num_igates		= 1,
	.num_ogates		= 1,
	.process_batch 		= macswap_process_batch,
};

ADD_MCLASS(macswap)
