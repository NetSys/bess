#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include "../module.h"

/* TODO: Currently it decapulates the entire Ethernet/IP/UDP/VXLAN headers.
 *       Modularize. */

enum {
  ATTR_W_TUN_IP_SRC,
  ATTR_W_TUN_IP_DST,
  ATTR_W_TUN_ID,
};

class VXLANDecap : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  void ProcessBatch(struct pkt_batch *batch);

  int num_attrs = 3;
  struct mt_attr attrs[MAX_ATTRS_PER_MODULE] = {
      {
          .name = "tun_ip_src", .size = 4, .mode = MT_WRITE,
      },
      {
          .name = "tun_ip_dst", .size = 4, .mode = MT_WRITE,
      },
      {
          .name = "tun_id", .size = 4, .mode = MT_WRITE,
      },
  };
};

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

    set_attr(this, ATTR_W_TUN_IP_SRC, pkt, uint32_t, iph->src_addr);
    set_attr(this, ATTR_W_TUN_IP_DST, pkt, uint32_t, iph->dst_addr);
    set_attr(this, ATTR_W_TUN_ID, pkt, uint32_t,
             rte_be_to_cpu_32(vh->vx_vni) >> 8);

    snb_adj(pkt, sizeof(*ethh) + iph_bytes + sizeof(*udph) + sizeof(*vh));
  }

  run_next_module(this, batch);
}

ADD_MODULE(VXLANDecap, "vxlan_decap",
           "decapsulates the outer Ethetnet/IP/UDP/VXLAN headers")
