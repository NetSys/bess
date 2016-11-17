#include "ip_encap.h"

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>

enum {
  ATTR_R_IP_SRC,
  ATTR_R_IP_DST,
  ATTR_R_IP_PROTO,
  ATTR_W_IP_NEXTHOP,
  ATTR_W_ETHER_TYPE,
};

struct snobj *IPEncap::Init(struct snobj *arg [[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("ip_src", 4, AccessMode::kRead);
  AddMetadataAttr("ip_dst", 4, AccessMode::kRead);
  AddMetadataAttr("ip_proto", 1, AccessMode::kRead);
  AddMetadataAttr("ip_nexthop", 4, AccessMode::kWrite);
  AddMetadataAttr("ether_type", 2, AccessMode::kWrite);

  return nullptr;
};

pb_error_t IPEncap::InitPb(const bess::pb::IPEncapArg &arg [[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("ip_src", 4, AccessMode::kRead);
  AddMetadataAttr("ip_dst", 4, AccessMode::kRead);
  AddMetadataAttr("ip_proto", 1, AccessMode::kRead);
  AddMetadataAttr("ip_nexthop", 4, AccessMode::kWrite);
  AddMetadataAttr("ether_type", 2, AccessMode::kWrite);

  return pb_errno(0);
}

void IPEncap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];

    uint32_t ip_src = get_attr<uint32_t>(this, ATTR_R_IP_SRC, pkt);
    uint32_t ip_dst = get_attr<uint32_t>(this, ATTR_R_IP_DST, pkt);
    uint8_t ip_proto = get_attr<uint8_t>(this, ATTR_R_IP_PROTO, pkt);

    struct ipv4_hdr *iph;

    uint16_t total_len = snb_total_len(pkt) + sizeof(*iph);

    iph = static_cast<struct ipv4_hdr *>(snb_prepend(pkt, sizeof(*iph)));

    if (unlikely(!iph)) {
      continue;
    }

    iph->version_ihl = 0x45;
    iph->total_length = rte_cpu_to_be_16(total_len);
    iph->fragment_offset = rte_cpu_to_be_16(IPV4_HDR_DF_FLAG);
    iph->time_to_live = 64;
    iph->next_proto_id = ip_proto;
    iph->src_addr = ip_src;
    iph->dst_addr = ip_dst;

    iph->hdr_checksum = rte_ipv4_cksum(iph);

    set_attr<uint32_t>(this, ATTR_W_IP_NEXTHOP, pkt, ip_dst);
    set_attr<uint16_t>(this, ATTR_W_ETHER_TYPE, pkt,
                       rte_cpu_to_be_16(ETHER_TYPE_IPv4));
  }

  RunNextModule(batch);
}

ADD_MODULE(IPEncap, "ip_encap", "encapsulates packets with an IPv4 header")
