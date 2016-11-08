#include "vlan_split.h"

#include <rte_byteorder.h>

const Commands<Module> VLANSplit::cmds = {};
const PbCommands VLANSplit::pb_cmds = {};

void VLANSplit::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t vid[MAX_PKT_BURST];
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];
    char *old_head = static_cast<char *>(snb_head_data(pkt));
    __m128i ethh;
    uint16_t tpid;
    uint16_t tci;
    int tagged;

    ethh = _mm_loadu_si128((__m128i *)old_head);
    tpid = _mm_extract_epi16(ethh, 6);

    tagged = (tpid == rte_cpu_to_be_16(0x8100)) ||
             (tpid == rte_cpu_to_be_16(0x88a8));

    if (tagged && snb_adj(pkt, 4)) {
      tci = _mm_extract_epi16(ethh, 7);
      ethh = _mm_slli_si128(ethh, 4);
      _mm_storeu_si128((__m128i *)old_head, ethh);
      vid[i] = rte_be_to_cpu_16(tci) & 0x0fff;
    } else {
      vid[i] = 0; /* untagged packets go to gate 0 */
    }
  }

  RunSplit(vid, batch);
}

ADD_MODULE(VLANSplit, "vlan_split", "split packets depending on their VID")
