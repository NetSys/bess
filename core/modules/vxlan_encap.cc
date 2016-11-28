#include "vxlan_encap.h"

#include <netinet/in.h>

#include <rte_byteorder.h>
#include <rte_config.h>
#include <rte_ether.h>
#include <rte_hash_crc.h>
#include <rte_ip.h>
#include <rte_udp.h>

enum {
  ATTR_R_TUN_IP_SRC,
  ATTR_R_TUN_IP_DST,
  ATTR_R_TUN_ID,
  ATTR_W_IP_SRC,
  ATTR_W_IP_DST,
  ATTR_W_IP_PROTO,
};

pb_error_t VXLANEncap::Init(const bess::pb::VXLANEncapArg &arg) {
  auto dstport = arg.dstport();
  if (dstport == 0) {
    dstport_ = rte_cpu_to_be_16(4789);
  } else {
    if (dstport >= 65536)
      return pb_error(EINVAL, "invalid 'dstport' field");
    dstport_ = rte_cpu_to_be_16(dstport);
  }

  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("tun_ip_src", 4, AccessMode::kRead);
  AddMetadataAttr("tun_ip_dst", 4, AccessMode::kRead);
  AddMetadataAttr("tun_id", 4, AccessMode::kRead);
  AddMetadataAttr("ip_src", 4, AccessMode::kWrite);
  AddMetadataAttr("ip_dst", 4, AccessMode::kWrite);
  AddMetadataAttr("ip_proto", 1, AccessMode::kWrite);

  return pb_errno(0);
}

void VXLANEncap::ProcessBatch(bess::PacketBatch *batch) {
  uint16_t dstport = dstport_;
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    uint32_t ip_src = get_attr<uint32_t>(this, ATTR_R_TUN_IP_SRC, pkt);
    uint32_t ip_dst = get_attr<uint32_t>(this, ATTR_R_TUN_IP_DST, pkt);
    uint32_t vni = get_attr<uint32_t>(this, ATTR_R_TUN_ID, pkt);

    struct ether_hdr *inner_ethh;
    struct udp_hdr *udph;
    struct vxlan_hdr *vh;

    int inner_frame_len = pkt->total_len() + sizeof(*udph);

    inner_ethh = pkt->head_data<struct ether_hdr *>();
    udph = static_cast<struct udp_hdr *>(
        pkt->prepend(sizeof(*udph) + sizeof(*vh)));

    if (unlikely(!udph)) {
      continue;
    }

    vh = reinterpret_cast<struct vxlan_hdr *>(udph + 1);
    vh->vx_flags = rte_cpu_to_be_32(0x08000000);
    vh->vx_vni = rte_cpu_to_be_32(vni << 8);

    udph->src_port =
        rte_hash_crc(inner_ethh, ETHER_ADDR_LEN * 2, UINT32_MAX) | 0x00f0;
    udph->dst_port = dstport;
    udph->dgram_len = rte_cpu_to_be_16(sizeof(*udph) + inner_frame_len);
    udph->dgram_cksum = rte_cpu_to_be_16(0);

    set_attr<uint32_t>(this, ATTR_W_IP_SRC, pkt, ip_src);
    set_attr<uint32_t>(this, ATTR_W_IP_DST, pkt, ip_dst);
    set_attr<uint8_t>(this, ATTR_W_IP_PROTO, pkt, IPPROTO_UDP);
  }

  RunNextModule(batch);
}

ADD_MODULE(VXLANEncap, "vxlan_encap",
           "encapsulates packets with UDP/VXLAN headers")
