#include "ether_encap.h"

#include <rte_config.h>
#include <rte_ether.h>

enum {
  ATTR_R_ETHER_SRC,
  ATTR_R_ETHER_DST,
  ATTR_R_ETHER_TYPE,
};

pb_error_t EtherEncap::Init(
    const bess::pb::EtherEncapArg &arg[[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("ether_src", ETHER_ADDR_LEN, AccessMode::kRead);
  AddMetadataAttr("ether_dst", ETHER_ADDR_LEN, AccessMode::kRead);
  AddMetadataAttr("ether_type", 2, AccessMode::kRead);

  return pb_errno(0);
};

void EtherEncap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct ether_addr ether_src;
    struct ether_addr ether_dst;
    uint16_t ether_type;

    ether_src = get_attr<struct ether_addr>(this, ATTR_R_ETHER_SRC, pkt);
    ether_dst = get_attr<struct ether_addr>(this, ATTR_R_ETHER_DST, pkt);
    ether_type = get_attr<uint16_t>(this, ATTR_R_ETHER_TYPE, pkt);

    struct ether_hdr *ethh =
        static_cast<struct ether_hdr *>(pkt->prepend(sizeof(*ethh)));

    if (unlikely(!ethh)) {
      continue;
    }

    ethh->d_addr = ether_dst;
    ethh->s_addr = ether_src;
    ethh->ether_type = ether_type;
  }

  RunNextModule(batch);
}

ADD_MODULE(EtherEncap, "ether_encap",
           "encapsulates packets with an Ethernet header")
