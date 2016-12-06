#include <string.h>

#include <arpa/inet.h>

#include <rte_byteorder.h>

#include "../utils/format.h"
#include "../utils/simd.h"
#include "vlan_push.h"

const Commands VLANPush::cmds = {
    {"set_tci", "VLANPushArg", MODULE_CMD_FUNC(&VLANPush::CommandSetTci), 0},
};

pb_error_t VLANPush::Init(const bess::pb::VLANPushArg &arg) {
  pb_cmd_response_t response = CommandSetTci(arg);
  return response.error();
}

pb_cmd_response_t VLANPush::CommandSetTci(const bess::pb::VLANPushArg &arg) {
  pb_cmd_response_t response;

  uint16_t tci;
  tci = arg.tci();

  vlan_tag_ = htonl((0x8100 << 16) | tci);
  qinq_tag_ = htonl((0x88a8 << 16) | tci);
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

/* the behavior is undefined if a packet is already double tagged */
void VLANPush::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  uint32_t vlan_tag = vlan_tag_;
  uint32_t qinq_tag = qinq_tag_;

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    char *new_head;
    uint16_t tpid;

    if ((new_head = static_cast<char *>(pkt->prepend(4))) != nullptr) {
/* shift 12 bytes to the left by 4 bytes */
#if __SSE4_1__
      __m128i ethh;

      ethh = _mm_loadu_si128(reinterpret_cast<__m128i *>(new_head + 4));
      tpid = _mm_extract_epi16(ethh, 6);

      ethh = _mm_insert_epi32(
          ethh, (tpid == rte_cpu_to_be_16(0x8100)) ? qinq_tag : vlan_tag, 3);

      _mm_storeu_si128(reinterpret_cast<__m128i *>(new_head), ethh);
#else
      tpid = *(uint16_t *)(new_head + 16);
      memmove(new_head, new_head + 4, 12);

      *(uint32_t *)(new_head + 12) =
          (tpid == rte_cpu_to_be_16(0x8100)) ? qinq_tag : vlan_tag;
#endif
    }
  }

  RunNextModule(batch);
}

std::string VLANPush::GetDesc() const {
  uint32_t vlan_tag_cpu = ntohl(vlan_tag_);

  return bess::utils::Format(
      "PCP=%u DEI=%u VID=%u", (vlan_tag_cpu >> 13) & 0x0007,
      (vlan_tag_cpu >> 12) & 0x0001, vlan_tag_cpu & 0x0fff);
}

ADD_MODULE(VLANPush, "vlan_push", "adds 802.1Q/802.11ad VLAN tag")
