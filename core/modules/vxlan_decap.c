#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include "../module.h"

/* TODO: Currently it decapulates the entire Ethernet/IP/UDP/VXLAN headers.
 *       Modularize. */

enum {
	attr_w_tun_ip_src,
	attr_w_tun_ip_dst,
	attr_w_tun_id,
};

static void vxlan_decap_process_batch(struct module *m, struct pkt_batch *batch)
{
	int cnt = batch->cnt;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		struct ether_hdr *ethh = snb_head_data(pkt);
		struct ipv4_hdr *iph = (struct ipv4_hdr *)(ethh + 1);
		int iph_bytes = (iph->version_ihl & 0xf) << 2;
		struct udp_hdr *udph = ((void *)iph + iph_bytes);
		struct vxlan_hdr *vh = (struct vxlan_hdr *)(udph + 1);
	
		set_attr(m, attr_w_tun_ip_src, pkt, uint32_t, iph->src_addr);
		set_attr(m, attr_w_tun_ip_dst, pkt, uint32_t, iph->dst_addr);
		set_attr(m, attr_w_tun_id, pkt, uint32_t, 
				rte_be_to_cpu_32(vh->vx_vni) >> 8);

		snb_adj(pkt, sizeof(*ethh) + iph_bytes + 
				sizeof(*udph) + sizeof(*vh));
	}

	run_next_module(m, batch);
}

static const struct mclass vxlan_decap = {
	.name			= "VXLANDecap",
	.help			= 
		"decapsulates the outer Ethetnet/IP/UDP/VXLAN headers",
	.def_module_name	= "vxlan_decap",
	.num_igates		= 1,
	.num_ogates		= 1,
	.attrs			= {
		[attr_w_tun_ip_src] = {
			.name = "tun_ip_src",	
			.size = 4,	
			.mode = MT_WRITE,
		},
		[attr_w_tun_ip_dst] = {
			.name = "tun_ip_dst",	
			.size = 4,	
			.mode = MT_WRITE,
		},
		[attr_w_tun_id] = {
			.name = "tun_id",	
			.size = 4,	
			.mode = MT_WRITE,
		},
	},
	.process_batch		= vxlan_decap_process_batch,
};

ADD_MCLASS(vxlan_decap)
