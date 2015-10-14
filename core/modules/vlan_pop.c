#include <string.h>

#include "../module.h"

static void process_batch(struct module *m, struct pkt_batch *batch)
{
	for (int i = 0; i < batch->cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *ptr = snb_head_data(pkt);

		memmove(ptr + 4, ptr, 12);
		snb_adj(pkt, 4);
	}
		
	run_next_module(m, batch);
}

static const struct mclass vlan_pop = {
	.name 			= "VLANPop",
	.def_module_name 	= "vlan_pop",
	.process_batch  	= process_batch,
};

ADD_MCLASS(vlan_pop)
