#include <string.h>

#include "../module.h"

static void vpop_process_batch(struct module *m, struct pkt_batch *batch)
{
	int cnt = batch->cnt;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *old_head = snb_head_data(pkt);
		__m128i ethh;
		uint16_t tpid;
		int tagged;
	
		ethh = _mm_loadu_si128((__m128i *)old_head);
		tpid = _mm_extract_epi16(ethh, 6);

		tagged = (tpid == rte_cpu_to_be_16(0x8100)) ||
				(tpid == rte_cpu_to_be_16(0x88a8));

		if (tagged && snb_adj(pkt, 4)) {
			ethh = _mm_slli_si128(ethh, 4);
			_mm_storeu_si128((__m128i *)old_head, ethh);
		}
	}
		
	run_next_module(m, batch);
}

static const struct mclass vlan_pop = {
	.name 			= "VLANPop",
	.def_module_name 	= "vlan_pop",
	.process_batch  	= vpop_process_batch,
};

ADD_MCLASS(vlan_pop)
