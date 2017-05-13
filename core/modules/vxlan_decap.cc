#include "vxlan_decap.h"

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/udp.h"
#include "../utils/vxlan.h"

/* TODO: Currently it decapulates the entire Ethernet/IP/UDP/VXLAN headers.
 *       Modularize. */

enum {
  ATTR_W_TUN_IP_SRC,
  ATTR_W_TUN_IP_DST,
  ATTR_W_TUN_ID,
};

CommandResponse VXLANDecap::Init(
    const bess::pb::VXLANDecapArg &arg[[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("tun_ip_src", 4, AccessMode::kWrite);
  AddMetadataAttr("tun_ip_dst", 4, AccessMode::kWrite);
  AddMetadataAttr("tun_id", 4, AccessMode::kWrite);

  return CommandSuccess();
}

void VXLANDecap::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::be32_t;
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;
  using bess::utils::Vxlan;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
    size_t ip_bytes = ip->header_length << 2;
    Udp *udp =
        reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);
    Vxlan *vh = reinterpret_cast<Vxlan *>(udp + 1);

    set_attr<be32_t>(this, ATTR_W_TUN_IP_SRC, pkt, ip->src);
    set_attr<be32_t>(this, ATTR_W_TUN_IP_DST, pkt, ip->dst);
    set_attr<be32_t>(this, ATTR_W_TUN_ID, pkt, vh->vx_vni >> 8);

    pkt->adj(sizeof(*eth) + ip_bytes + sizeof(*udp) + sizeof(*vh));
  }

  RunNextModule(batch);
}

ADD_MODULE(VXLANDecap, "vxlan_decap",
           "decapsulates the outer Ethetnet/IP/UDP/VXLAN headers")
