#include "vlan_push.h"

#include <cstring>

#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/simd.h"

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::Ethernet;

const Commands VLANPush::cmds = {
    {"set_tci", "VLANPushArg", MODULE_CMD_FUNC(&VLANPush::CommandSetTci), 0},
};

CommandResponse VLANPush::Init(const bess::pb::VLANPushArg &arg) {
  return CommandSetTci(arg);
}

CommandResponse VLANPush::CommandSetTci(const bess::pb::VLANPushArg &arg) {
  uint16_t tci = arg.tci();
  vlan_tag_ = be32_t((Ethernet::Type::kVlan << 16) | tci);
  qinq_tag_ = be32_t((Ethernet::Type::kQinQ << 16) | tci);
  return CommandSuccess();
}

/* the behavior is undefined if a packet is already double tagged */
void VLANPush::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  be32_t vlan_tag = vlan_tag_;
  be32_t qinq_tag = qinq_tag_;

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    char *new_head;

    if ((new_head = static_cast<char *>(pkt->prepend(4))) != nullptr) {
/* shift 12 bytes to the left by 4 bytes */
#if __SSE4_1__
      __m128i ethh;

      ethh = _mm_loadu_si128(reinterpret_cast<__m128i *>(new_head + 4));
      be16_t tpid(be16_t::swap(_mm_extract_epi16(ethh, 6)));

      ethh = _mm_insert_epi32(ethh, (tpid.value() == Ethernet::Type::kVlan)
                                        ? qinq_tag.raw_value()
                                        : vlan_tag.raw_value(),
                              3);

      _mm_storeu_si128(reinterpret_cast<__m128i *>(new_head), ethh);
#else
      be16_t tpid(*(uint16_t *)(new_head + 16));
      memmove(new_head, new_head + 4, 12);

      *(be32_t *)(new_head + 12) =
          (tpid.value() == Ethernet::Type::kVlan) ? qinq_tag : vlan_tag;
#endif
    }
  }

  RunNextModule(batch);
}

std::string VLANPush::GetDesc() const {
  uint32_t vlan_tag = vlan_tag_.value();

  return bess::utils::Format("PCP=%u DEI=%u VID=%u", (vlan_tag >> 13) & 0x0007,
                             (vlan_tag >> 12) & 0x0001, vlan_tag & 0x0fff);
}

ADD_MODULE(VLANPush, "vlan_push", "adds 802.1Q/802.11ad VLAN tag")
