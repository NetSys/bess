#include "vlan_split.h"

#include "../utils/ether.h"

void VLANSplit::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::be16_t;
  using bess::utils::Ethernet;

  gate_idx_t vid[bess::PacketBatch::kMaxBurst];
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    char *old_head = pkt->head_data<char *>();
    __m128i eth;

    eth = _mm_loadu_si128(reinterpret_cast<__m128i *>(old_head));
    be16_t tpid(be16_t::swap(_mm_extract_epi16(eth, 6)));

    bool tagged = (tpid == be16_t(Ethernet::Type::kVlan)) ||
                  (tpid == be16_t(Ethernet::Type::kQinQ));

    if (tagged && pkt->adj(4)) {
      be16_t tci(be16_t::swap(_mm_extract_epi16(eth, 7)));
      eth = _mm_slli_si128(eth, 4);
      _mm_storeu_si128(reinterpret_cast<__m128i *>(old_head), eth);
      vid[i] = tci.value() & 0x0fff;
    } else {
      vid[i] = 0; /* untagged packets go to gate 0 */
    }
  }

  RunSplit(vid, batch);
}

ADD_MODULE(VLANSplit, "vlan_split", "split packets depending on their VID")
