#include <netinet/in.h>

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_byteorder.h>
#include <rte_hash_crc.h>

#include "../module.h"

enum {
	attr_r_tun_ip_src,
	attr_r_tun_ip_dst,
	attr_r_tun_id,
	attr_w_ip_src,
	attr_w_ip_dst,
	attr_w_ip_proto,
};

struct vxlan_encap_priv {
	uint16_t dstport;
};

static struct snobj *vxlan_encap_init(struct module *m, struct snobj *arg)
{
	struct vxlan_encap_priv *priv = get_priv(m);

	priv->dstport = rte_cpu_to_be_16(4789);

	if (arg) {
		int dstport = snobj_eval_uint(arg, "dstport");
		if (dstport <= 0 || dstport >= 65536)
			return snobj_err(EINVAL, "invalid 'dstport' field");
		priv->dstport = rte_cpu_to_be_16(dstport);
	}

	return NULL;
}

static void vxlan_encap_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct vxlan_encap_priv *priv = get_priv(m);
	uint16_t dstport = priv->dstport;
	int cnt = batch->cnt;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];

		uint32_t ip_src = get_attr(m, attr_r_tun_ip_src, pkt, uint32_t);
		uint32_t ip_dst = get_attr(m, attr_r_tun_ip_dst, pkt, uint32_t);
		uint32_t vni = get_attr(m, attr_r_tun_id, pkt, uint32_t);

		struct ether_hdr *inner_ethh;
		struct udp_hdr *udph;
		struct vxlan_hdr *vh;

		int inner_frame_len = snb_total_len(pkt) + sizeof(*udph);

		inner_ethh = snb_head_data(pkt);
		udph = snb_prepend(pkt, sizeof(*udph) + sizeof(*vh));

		if (unlikely(!udph))
			continue;

		vh = (struct vxlan_hdr *)(udph + 1);
		vh->vx_flags = rte_cpu_to_be_32(0x08000000);
		vh->vx_vni = rte_cpu_to_be_32(vni << 8);

		udph->src_port = rte_hash_crc(inner_ethh, ETHER_ADDR_LEN * 2,
				UINT32_MAX) | 0x00f0;
		udph->dst_port = dstport;
		udph->dgram_len = rte_cpu_to_be_16(sizeof(*udph) + 
				inner_frame_len);
		udph->dgram_cksum = rte_cpu_to_be_16(0);

		set_attr(m, attr_w_ip_src, pkt, uint32_t, ip_src);
		set_attr(m, attr_w_ip_dst, pkt, uint32_t, ip_dst);
		set_attr(m, attr_w_ip_proto, pkt, uint8_t, IPPROTO_UDP);
	}

	run_next_module(m, batch);
}

static const struct mclass vxlan_encap = {
	.name			= "VXLANEncap",
	.help			= 
		"encapsulates packets with UDP/VXLAN headers",
	.def_module_name	= "vxlan_encap",
	.num_igates		= 1,
	.num_ogates		= 1,
	.priv_size		= sizeof(struct vxlan_encap_priv),
	.attrs			= {
		[attr_r_tun_ip_src] = {
			.name = "tun_ip_src",	
			.size = 4,	
			.mode = MT_READ,
		},
		[attr_r_tun_ip_dst] = {
			.name = "tun_ip_dst",	
			.size = 4,	
			.mode = MT_READ,
		},
		[attr_r_tun_id] = {
			.name = "tun_id",	
			.size = 4,	
			.mode = MT_READ,
		},
		[attr_w_ip_src] = {
			.name = "ip_src",	
			.size = 4,	
			.mode = MT_WRITE,
		},
		[attr_w_ip_dst] = {
			.name = "ip_dst",	
			.size = 4,	
			.mode = MT_WRITE,
		},
		[attr_w_ip_proto] = {
			.name = "ip_proto",
			.size = 1,	
			.mode = MT_WRITE,
		},
	},
	.init			= vxlan_encap_init,
	.process_batch		= vxlan_encap_process_batch,
};

ADD_MCLASS(vxlan_encap)
