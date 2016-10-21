#include <rte_config.h>
#include <rte_ether.h>

#include "../module.h"

enum {
  ATTR_R_ETHER_SRC,
  ATTR_R_ETHER_DST,
  ATTR_R_ETHER_TYPE,
};

class EtherEncap : public Module {
 public:
  void ProcessBatch(struct pkt_batch *batch);

  int num_attrs = 5;
  struct mt_attr attrs[MAX_ATTRS_PER_MODULE] = {
      {
          .name = "ether_src", .size = ETHER_ADDR_LEN, .mode = MT_READ,
      },
      {
          .name = "ether_dst", .size = ETHER_ADDR_LEN, .mode = MT_READ,
      },
      {
          .name = "ether_type", .size = 2, .mode = MT_READ,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
};

const Commands<Module> EtherEncap::cmds = {};

void EtherEncap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];

    struct ether_addr ether_src;
    struct ether_addr ether_dst;
    uint16_t ether_type;

    ether_src = get_attr(this, ATTR_R_ETHER_SRC, pkt, struct ether_addr);
    ether_dst = get_attr(this, ATTR_R_ETHER_DST, pkt, struct ether_addr);
    ether_type = get_attr(this, ATTR_R_ETHER_TYPE, pkt, uint16_t);

    struct ether_hdr *ethh =
        static_cast<struct ether_hdr *>(snb_prepend(pkt, sizeof(*ethh)));

    if (unlikely(!ethh)) continue;

    ethh->d_addr = ether_dst;
    ethh->s_addr = ether_src;
    ethh->ether_type = ether_type;
  }

  RunNextModule(batch);
}

ADD_MODULE(EtherEncap, "ether_encap",
           "encapsulates packets with an Ethernet header")
