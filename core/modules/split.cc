#include "split.h"

#include <rte_byteorder.h>

#define MAX_SIZE 8

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

// XXX: this is repeated in many modules. get rid of them when converting .h to
// .hh, etc... it's in defined in some old header
static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

pb_error_t Split::Init(const bess::pb::SplitArg &arg) {
  size_ = arg.size();
  if (size_ < 1 || size_ > MAX_SIZE) {
    return pb_error(EINVAL, "'size' must be 1-%d", MAX_SIZE);
  }
  mask_ = ((uint64_t)1 << (size_ * 8)) - 1;
  const char *name = arg.name().c_str();
  if (arg.name().length()) {
    attr_id_ = AddMetadataAttr(name, size_,
                               bess::metadata::Attribute::AccessMode::kRead);
    if (attr_id_ < 0)
      return pb_error(-attr_id_, "add_metadata_attr() failed");
  } else {
    attr_id_ = -1;
    offset_ = arg.offset();
    if (offset_ < 0 || offset_ > 1024) {
      return pb_error(EINVAL, "invalid 'offset'");
    }
  }
  return pb_errno(0);
}

void Split::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t ogate[bess::PacketBatch::kMaxBurst];
  int cnt = batch->cnt();

  if (attr_id_ >= 0) {
    int attr_id = attr_id_;

    for (int i = 0; i < cnt; i++) {
      bess::Packet *pkt = batch->pkts()[i];

      uint64_t val = get_attr<uint64_t>(this, attr_id, pkt);
      val &= mask_;

      if (is_valid_gate(val)) {
        ogate[i] = val;
      } else {
        ogate[i] = DROP_GATE;
      }
    }
  } else {
    int offset = offset_;

    for (int i = 0; i < cnt; i++) {
      bess::Packet *pkt = batch->pkts()[i];
      char *head = pkt->head_data<char *>();

      uint64_t val = *reinterpret_cast<uint64_t *>(head + offset);
      val = rte_be_to_cpu_64(val) & mask_;

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
