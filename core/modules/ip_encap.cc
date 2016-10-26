#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include "../module.h"

enum {
  ATTR_R_IP_SRC,
  ATTR_R_IP_DST,
  ATTR_R_IP_PROTO,
  ATTR_W_IP_NEXTHOP,
  ATTR_W_ETHER_TYPE,
};

class IPEncap : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  int num_attrs = 5;
  struct mt_attr attrs[MAX_ATTRS_PER_MODULE] = {
      {
          .name = "ip_src", .size = 4, .mode = MT_READ,
      },
      {
          .name = "ip_dst", .size = 4, .mode = MT_READ,
      },
      {
          .name = "ip_proto", .size = 1, .mode = MT_READ,
      },
      {
          .name = "ip_nexthop", .size = 4, .mode = MT_WRITE,
      },
      {
          .name = "ether_type", .size = 2, .mode = MT_WRITE,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
};

const Commands<Module> IPEncap::cmds = {};

void IPEncap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];

    uint32_t ip_src = GET_ATTR(this, ATTR_R_IP_SRC, pkt, uint32_t);
    uint32_t ip_dst = GET_ATTR(this, ATTR_R_IP_DST, pkt, uint32_t);
    uint8_t ip_proto = GET_ATTR(this, ATTR_R_IP_PROTO, pkt, uint8_t);

    struct ipv4_hdr *iph;

    uint16_t total_len = snb_total_len(pkt) + sizeof(*iph);

    iph = static_cast<struct ipv4_hdr *>(snb_prepend(pkt, sizeof(*iph)));

    if (BESS_UNLIKELY(!iph)) {
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

    SET_ATTR(this, ATTR_W_IP_NEXTHOP, pkt, uint32_t, ip_dst);
    SET_ATTR(this, ATTR_W_ETHER_TYPE, pkt, uint16_t,
             rte_cpu_to_be_16(ETHER_TYPE_IPv4));
  }

  RunNextModule(batch);
}

ADD_MODULE(IPEncap, "ip_encap", "encapsulates packets with an IPv4 header")
