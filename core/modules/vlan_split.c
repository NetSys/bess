#include <string.h>

#include "../module.h"

/* 
 * NOTE:
 *   1. Order is preserved for packets with the same gate.
 *   2. No ordering guarantee for packets with different gates.
 *   3. Array ogates may be altered.
 *
 * TODO: optimize this function. currently O(n^2) in the worst case
 */
static void run_split(struct module *m, uint16_t *ogates,
		const struct pkt_batch *org)
{
	int i = 0;

	while (i < org->cnt) {
		uint16_t h = ogates[i];

		if (h != INVALID_GATE) {
			struct pkt_batch batch;
			int cnt = 1;

			batch.pkts[0] = org->pkts[i++];

			for (int j = i + 1; j < org->cnt; j++) {
				uint16_t t = ogates[j];

				if (h == t) {
					batch.pkts[cnt++] = org->pkts[j];
					ogates[j] = INVALID_GATE;
					i += (i == j);
				}
			}

			batch.cnt = cnt;

			run_choose_module(m, h, &batch);
		} else
			i++;
	}
}

static void process_batch(struct module *m, struct pkt_batch *batch)
{
	uint16_t vid[MAX_PKT_BURST];

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
	.process_batch  	= process_batch,
};

ADD_MCLASS(vlan_split)
