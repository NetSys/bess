#include "ether_encap.h"

#include "../utils/ether.h"

using bess::utils::EthHeader;

enum {
  ATTR_R_ETHER_SRC,
  ATTR_R_ETHER_DST,
  ATTR_R_ETHER_TYPE,
};

CommandResponse EtherEncap::Init(
    const bess::pb::EtherEncapArg &arg[[maybe_unused]]) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("ether_src", sizeof(EthHeader::Address), AccessMode::kRead);
  AddMetadataAttr("ether_dst", sizeof(EthHeader::Address), AccessMode::kRead);
  AddMetadataAttr("ether_type", 2, AccessMode::kRead);

  return CommandSuccess();
};

void EtherEncap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    EthHeader::Address ether_src;
    EthHeader::Address ether_dst;
    bess::utils::be16_t ether_type;

    ether_src = get_attr<EthHeader::Address>(this, ATTR_R_ETHER_SRC, pkt);
    ether_dst = get_attr<EthHeader::Address>(this, ATTR_R_ETHER_DST, pkt);
    ether_type = get_attr<bess::utils::be16_t>(this, ATTR_R_ETHER_TYPE, pkt);

    EthHeader *eth = static_cast<EthHeader *>(pkt->prepend(sizeof(*eth)));

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
