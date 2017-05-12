#include "split.h"

#include "../utils/endian.h"

// XXX: this is repeated in many modules. get rid of them when converting .h to
// .hh, etc... it's in defined in some old header
static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

CommandResponse Split::Init(const bess::pb::SplitArg &arg) {
  size_ = arg.size();
  if (size_ < 1 || size_ > sizeof(uint64_t)) {
    return CommandFailure(EINVAL, "'size' must be 1-%zu", sizeof(uint64_t));
  }

  mask_ = (size_ == 8) ? 0xffffffffffffffffull : (1ull << (size_ * 8)) - 1;

  if (arg.type_case() == bess::pb::SplitArg::kAttribute) {
    attr_id_ = AddMetadataAttr(arg.attribute().c_str(), size_,
                               bess::metadata::Attribute::AccessMode::kRead);
    if (attr_id_ < 0) {
      return CommandFailure(-attr_id_, "add_metadata_attr() failed");
    }
  } else {
    attr_id_ = -1;
    offset_ = arg.offset();
    if (offset_ > 1024) {
      return CommandFailure(EINVAL, "invalid 'offset'");
    }
  }
  return CommandSuccess();
}

void Split::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::be64_t;

  gate_idx_t ogate[bess::PacketBatch::kMaxBurst];
  int cnt = batch->cnt();

  if (attr_id_ >= 0) {
    int attr_id = attr_id_;

    for (int i = 0; i < cnt; i++) {
      const bess::Packet *pkt = batch->pkts()[i];

      uint64_t val = get_attr<be64_t>(this, attr_id, pkt).value();
      val &= mask_;

      if (is_valid_gate(val)) {
        ogate[i] = val;
      } else {
        ogate[i] = DROP_GATE;
      }
    }
  } else {
    for (int i = 0; i < cnt; i++) {
      const bess::Packet *pkt = batch->pkts()[i];
      uint64_t val = (*(pkt->head_data<be64_t *>(offset_))).value() & mask_;

      if (is_valid_gate(val)) {
        ogate[i] = val;
      } else {
        ogate[i] = DROP_GATE;
      }
    }
  }

  RunSplit(ogate, batch);
}

ADD_MODULE(Split, "split",
           "split packets depending on packet data or metadata attributes")
