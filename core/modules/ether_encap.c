#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ether.h>

#include "../module.h"

enum {
	attr_r_ether_src,
	attr_r_ether_dst,
	attr_r_ether_type,
};

static void ether_encap_process_batch(struct module *m, struct pkt_batch *batch)
{
	int cnt = batch->cnt;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];

		struct ether_addr ether_src;
		struct ether_addr ether_dst;
		uint16_t ether_type;

		ether_src = get_attr(m, attr_r_ether_src, pkt, 
				struct ether_addr);
		ether_dst = get_attr(m, attr_r_ether_dst, pkt, 
				struct ether_addr);
		ether_type = get_attr(m, attr_r_ether_type, pkt, uint16_t);

		struct ether_hdr *ethh = snb_prepend(pkt, sizeof(*ethh));

		if (unlikely(!ethh))
			continue;

		*ethh = (struct ether_hdr){
			.d_addr = ether_dst,
			.s_addr = ether_src,
			.ether_type = ether_type,
		};
	}

	run_next_module(m, batch);
}

static const struct mclass ether_encap = {
	.name			= "EtherEncap",
	.help			= 
		"encapsulates packets with an Ethernet header",
	.def_module_name	= "ether_encap",
	.num_igates		= 1,
	.num_ogates		= 1,
	.attrs			= {
		[attr_r_ether_src] = {
			.name = "ether_src",
			.size = ETHER_ADDR_LEN,
			.mode = MT_READ,
		},
		[attr_r_ether_dst] = {
			.name = "ether_dst",
			.size = ETHER_ADDR_LEN,	
			.mode = MT_READ,
		},
		[attr_r_ether_type] = {
			.name = "ether_type",
			.size = 2,
			.mode = MT_READ,
		},
	},
	.process_batch		= ether_encap_process_batch,
};

ADD_MCLASS(ether_encap)
