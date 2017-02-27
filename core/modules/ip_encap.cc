#include "ip_encap.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

using bess::utils::EthHeader;
using bess::utils::Ipv4Header;

enum {
  ATTR_R_IP_SRC,
  ATTR_R_IP_DST,
  ATTR_R_IP_PROTO,
  ATTR_W_IP_NEXTHOP,
  ATTR_W_ETHER_TYPE,
};

pb_error_t IPEncap::Init(const bess::pb::IPEncapArg &arg[[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("ip_src", 4, AccessMode::kRead);
  AddMetadataAttr("ip_dst", 4, AccessMode::kRead);
  AddMetadataAttr("ip_proto", 1, AccessMode::kRead);
  AddMetadataAttr("ip_nexthop", 4, AccessMode::kWrite);
  AddMetadataAttr("ether_type", 2, AccessMode::kWrite);

  return pb_errno(0);
}

void IPEncap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    uint32_t ip_src = get_attr<uint32_t>(this, ATTR_R_IP_SRC, pkt);
    uint32_t ip_dst = get_attr<uint32_t>(this, ATTR_R_IP_DST, pkt);
    uint8_t ip_proto = get_attr<uint8_t>(this, ATTR_R_IP_PROTO, pkt);

    Ipv4Header *iph;

    uint16_t total_len = pkt->total_len() + sizeof(*iph);

    iph = static_cast<Ipv4Header *>(pkt->prepend(sizeof(*iph)));

    if (unlikely(!iph)) {
      continue;
    }

    iph->version = 0x4;
    iph->header_length = sizeof(*iph) / 4;
    iph->type_of_service = 0;
    iph->length = __builtin_bswap16(total_len);
    iph->fragment_offset = __builtin_bswap16(Ipv4Header::Flag::kDF);
    iph->ttl = 64;
    iph->protocol = ip_proto;
    iph->src = ip_src;
    iph->dst = ip_dst;

    iph->checksum = bess::utils::CalculateIpv4NoOptChecksum(*iph);

    set_attr<uint32_t>(this, ATTR_W_IP_NEXTHOP, pkt, ip_dst);
    set_attr<uint16_t>(this, ATTR_W_ETHER_TYPE, pkt,
                       __builtin_bswap16(EthHeader::Type::kIpv4));
  }

  RunNextModule(batch);
}

ADD_MODULE(IPEncap, "ip_encap", "encapsulates packets with an IPv4 header")
