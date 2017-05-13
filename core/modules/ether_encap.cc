#include "ether_encap.h"

#include "../utils/ether.h"

using bess::utils::Ethernet;

enum {
  ATTR_R_ETHER_SRC,
  ATTR_R_ETHER_DST,
  ATTR_R_ETHER_TYPE,
};

CommandResponse EtherEncap::Init(
    const bess::pb::EtherEncapArg &arg[[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("ether_src", sizeof(Ethernet::Address), AccessMode::kRead);
  AddMetadataAttr("ether_dst", sizeof(Ethernet::Address), AccessMode::kRead);
  AddMetadataAttr("ether_type", 2, AccessMode::kRead);

  return CommandSuccess();
};

void EtherEncap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet::Address ether_src;
    Ethernet::Address ether_dst;
    bess::utils::be16_t ether_type;

    ether_src = get_attr<Ethernet::Address>(this, ATTR_R_ETHER_SRC, pkt);
    ether_dst = get_attr<Ethernet::Address>(this, ATTR_R_ETHER_DST, pkt);
    ether_type = get_attr<bess::utils::be16_t>(this, ATTR_R_ETHER_TYPE, pkt);

    Ethernet *eth = static_cast<Ethernet *>(pkt->prepend(sizeof(*eth)));

    // not enough headroom?
    if (unlikely(!eth)) {
      continue;
    }

    eth->dst_addr = ether_dst;
    eth->src_addr = ether_src;
    eth->ether_type = ether_type;
  }

  RunNextModule(batch);
}

ADD_MODULE(EtherEncap, "ether_encap",
           "encapsulates packets with an Ethernet header")
