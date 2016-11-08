#include "vlan_pop.h"

#include <rte_byteorder.h>

const Commands<Module> VLANPop::cmds = {};
const PbCommands VLANPop::pb_cmds = {};

void VLANPop::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];
    char *old_head = static_cast<char *>(snb_head_data(pkt));
    __m128i ethh;
    uint16_t tpid;
    int tagged;

    ethh = _mm_loadu_si128((__m128i *)old_head);
    tpid = _mm_extract_epi16(ethh, 6);

    tagged = (tpid == rte_cpu_to_be_16(0x8100)) ||
             (tpid == rte_cpu_to_be_16(0x88a8));

    if (tagged && snb_adj(pkt, 4)) {
      ethh = _mm_slli_si128(ethh, 4);
      _mm_storeu_si128((__m128i *)old_head, ethh);
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(VLANPop, "vlan_pop", "removes 802.1Q/802.11ad VLAN tag")
