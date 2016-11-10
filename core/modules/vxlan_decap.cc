#include "vxlan_decap.h"

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

/* TODO: Currently it decapulates the entire Ethernet/IP/UDP/VXLAN headers.
 *       Modularize. */

enum {
  ATTR_W_TUN_IP_SRC,
  ATTR_W_TUN_IP_DST,
  ATTR_W_TUN_ID,
};

const Commands<Module> VXLANDecap::cmds = {};
const PbCommands VXLANDecap::pb_cmds = {};

struct snobj *VXLANDecap::Init(struct snobj *arg [[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("tun_ip_src", 4, AccessMode::kWrite);
  AddMetadataAttr("tun_ip_dst", 4, AccessMode::kWrite);
  AddMetadataAttr("tun_id", 4, AccessMode::kWrite);

  return nullptr;
};

pb_error_t VXLANDecap::InitPb(const bess::pb::VXLANDecapArg &arg [[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("tun_ip_src", 4, AccessMode::kWrite);
  AddMetadataAttr("tun_ip_dst", 4, AccessMode::kWrite);
  AddMetadataAttr("tun_id", 4, AccessMode::kWrite);

  return pb_errno(0);
}

void VXLANDecap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];
    struct ether_hdr *ethh =
        static_cast<struct ether_hdr *>(snb_head_data(pkt));
    struct ipv4_hdr *iph = reinterpret_cast<struct ipv4_hdr *>(ethh + 1);
    int iph_bytes = (iph->version_ihl & 0xf) << 2;
    struct udp_hdr *udph = reinterpret_cast<struct udp_hdr *>(
        reinterpret_cast<uint8_t *>(iph) + iph_bytes);
    struct vxlan_hdr *vh = reinterpret_cast<struct vxlan_hdr *>(udph + 1);

    set_attr<uint32_t>(this, ATTR_W_TUN_IP_SRC, pkt, iph->src_addr);
    set_attr<uint32_t>(this, ATTR_W_TUN_IP_DST, pkt, iph->dst_addr);
    set_attr<uint32_t>(this, ATTR_W_TUN_ID, pkt,
                       rte_be_to_cpu_32(vh->vx_vni) >> 8);

    snb_adj(pkt, sizeof(*ethh) + iph_bytes + sizeof(*udph) + sizeof(*vh));
  }

  RunNextModule(batch);
}

ADD_MODULE(VXLANDecap, "vxlan_decap",
           "decapsulates the outer Ethetnet/IP/UDP/VXLAN headers")
