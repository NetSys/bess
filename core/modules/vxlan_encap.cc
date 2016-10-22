#include <netinet/in.h>

#include <rte_byteorder.h>
#include <rte_config.h>
#include <rte_ether.h>
#include <rte_hash_crc.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include "../module.h"

enum {
  ATTR_R_TUN_IP_SRC,
  ATTR_R_TUN_IP_DST,
  ATTR_R_TUN_ID,
  ATTR_W_IP_SRC,
  ATTR_W_IP_DST,
  ATTR_W_IP_PROTO,
};

class VXLANEncap : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  int num_attrs = 6;
  struct mt_attr attrs[MAX_ATTRS_PER_MODULE] = {
      {
          .name = "tun_ip_src", .size = 4, .mode = MT_READ,
      },
      {
          .name = "tun_ip_dst", .size = 4, .mode = MT_READ,
      },
      {
          .name = "tun_id", .size = 4, .mode = MT_READ,
      },
      {
          .name = "ip_src", .size = 4, .mode = MT_WRITE,
      },
      {
          .name = "ip_dst", .size = 4, .mode = MT_WRITE,
      },
      {
          .name = "ip_proto", .size = 1, .mode = MT_WRITE,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  uint16_t dstport_;
};

const Commands<Module> VXLANEncap::cmds = {};

struct snobj *VXLANEncap::Init(struct snobj *arg) {
  dstport_ = rte_cpu_to_be_16(4789);

  if (arg) {
    int dstport = snobj_eval_uint(arg, "dstport");
    if (dstport <= 0 || dstport >= 65536)
      return snobj_err(EINVAL, "invalid 'dstport' field");
    dstport_ = rte_cpu_to_be_16(dstport);
  }

  return NULL;
}

void VXLANEncap::ProcessBatch(struct pkt_batch *batch) {
  uint16_t dstport = dstport_;
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];

    uint32_t ip_src = get_attr(this, ATTR_R_TUN_IP_SRC, pkt, uint32_t);
    uint32_t ip_dst = get_attr(this, ATTR_R_TUN_IP_DST, pkt, uint32_t);
    uint32_t vni = get_attr(this, ATTR_R_TUN_ID, pkt, uint32_t);

    struct ether_hdr *inner_ethh;
    struct udp_hdr *udph;
    struct vxlan_hdr *vh;

    int inner_frame_len = snb_total_len(pkt) + sizeof(*udph);

    inner_ethh = static_cast<struct ether_hdr *>(snb_head_data(pkt));
    udph = static_cast<struct udp_hdr *>(
        snb_prepend(pkt, sizeof(*udph) + sizeof(*vh)));

    if (unlikely(!udph)) continue;

    vh = reinterpret_cast<struct vxlan_hdr *>(udph + 1);
    vh->vx_flags = rte_cpu_to_be_32(0x08000000);
    vh->vx_vni = rte_cpu_to_be_32(vni << 8);

    udph->src_port =
        rte_hash_crc(inner_ethh, ETHER_ADDR_LEN * 2, UINT32_MAX) | 0x00f0;
    udph->dst_port = dstport;
    udph->dgram_len = rte_cpu_to_be_16(sizeof(*udph) + inner_frame_len);
    udph->dgram_cksum = rte_cpu_to_be_16(0);

    set_attr(this, ATTR_W_IP_SRC, pkt, uint32_t, ip_src);
    set_attr(this, ATTR_W_IP_DST, pkt, uint32_t, ip_dst);
    set_attr(this, ATTR_W_IP_PROTO, pkt, uint8_t, IPPROTO_UDP);
  }

  RunNextModule(batch);
}

ADD_MODULE(VXLANEncap, "vxlan_encap",
           "encapsulates packets with UDP/VXLAN headers")
