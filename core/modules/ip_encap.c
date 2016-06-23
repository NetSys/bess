#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include "../module.h"

enum {
	attr_r_ip_src,
	attr_r_ip_dst,
	attr_r_ip_proto,
	attr_w_ip_nexthop,
	attr_w_ether_type,
};

static void ip_encap_process_batch(struct module *m, struct pkt_batch *batch)
{
	int cnt = batch->cnt;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];

		uint32_t ip_src = get_attr(m, attr_r_ip_src, pkt, uint32_t);
		uint32_t ip_dst = get_attr(m, attr_r_ip_dst, pkt, uint32_t);
		uint8_t ip_proto = get_attr(m, attr_r_ip_proto, pkt, uint8_t);

		struct ipv4_hdr *iph;

		uint16_t total_len = snb_total_len(pkt) + sizeof(*iph);

		iph = snb_prepend(pkt, sizeof(*iph));

		if (unlikely(!iph))
			continue;

		*iph = (struct ipv4_hdr){
			.version_ihl = 0x45,
			.total_length = rte_cpu_to_be_16(total_len),
			.fragment_offset = rte_cpu_to_be_16(IPV4_HDR_DF_FLAG),
			.time_to_live = 64,
			.next_proto_id = ip_proto,
			.src_addr = ip_src,
			.dst_addr = ip_dst,
		};

		iph->hdr_checksum = rte_ipv4_cksum(iph);

		set_attr(m, attr_w_ip_nexthop, pkt, uint32_t, ip_dst);
		set_attr(m, attr_w_ether_type, pkt, uint16_t,
				rte_cpu_to_be_16(ETHER_TYPE_IPv4));
	}

	run_next_module(m, batch);
}

static const struct mclass ip_encap = {
	.name			= "IPEncap",
	.help			= 
		"encapsulates packets with an IPv4 header",
	.def_module_name	= "ip_encap",
	.num_igates		= 1,
	.num_ogates		= 1,
	.attrs			= {
		[attr_r_ip_src] = {
			.name = "ip_src",
			.size = 4,	
			.mode = MT_READ,
		},
		[attr_r_ip_dst] = {
			.name = "ip_dst",
			.size = 4,	
			.mode = MT_READ,
		},
		[attr_r_ip_proto] = {
			.name = "ip_proto",
			.size = 1,	
			.mode = MT_READ,
		},
		[attr_w_ip_nexthop] = {
			.name = "ip_nexthop",
			.size = 4,	
			.mode = MT_WRITE,
		},
		[attr_w_ether_type] = {
			.name = "ether_type",
			.size = 2,	
			.mode = MT_WRITE,
		},
	},
	.process_batch		= ip_encap_process_batch,
};

ADD_MCLASS(ip_encap)
