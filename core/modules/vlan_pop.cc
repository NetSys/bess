#include "vlan_pop.h"

#include <rte_byteorder.h>

void VLANPop::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    char *old_head = pkt->head_data<char *>();
    __m128i ethh;
    uint16_t tpid;
    int tagged;

    ethh = _mm_loadu_si128(reinterpret_cast<__m128i *>(old_head));
    tpid = _mm_extract_epi16(ethh, 6);

    tagged = (tpid == rte_cpu_to_be_16(0x8100)) ||
             (tpid == rte_cpu_to_be_16(0x88a8));

    if (tagged && pkt->adj(4)) {
      ethh = _mm_slli_si128(ethh, 4);
      _mm_storeu_si128(reinterpret_cast<__m128i *>(old_head), ethh);
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(VLANPop, "vlan_pop", "removes 802.1Q/802.11ad VLAN tag")
