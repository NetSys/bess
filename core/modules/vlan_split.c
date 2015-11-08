#include <string.h>

#include "../module.h"

static void vsplit_process_batch(struct module *m, struct pkt_batch *batch)
{
	gate_t vid[MAX_PKT_BURST];

	for (int i = 0; i < batch->cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *ptr = snb_head_data(pkt);
		uint32_t tag = rte_be_to_cpu_32(*((uint32_t *)(ptr + 12)));

		/* VLAN tagged? */
		if (tag >> 16 == 0x8100 || tag >> 16 == 0x88a8) {
			/* pop */
			memmove(ptr + 4, ptr, 12);
			snb_adj(pkt, 4);

			vid[i] = tag & 0xfff;
		} else
			vid[i] = 0;	/* untagged packets go to gate 0 */
	}
	
	run_split(m, vid, batch);
}

static const struct mclass vlan_split = {
	.name 			= "VLANSplit",
	.def_module_name 	= "vlan_split",
	.process_batch  	= vsplit_process_batch,
};

ADD_MCLASS(vlan_split)
