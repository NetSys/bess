#include <rte_byteorder.h>

#include "../module.h"

#define MAX_SIZE 8

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

// XXX: this is repeated in many modules. get rid of them when converting .h to
// .hh, etc... it's in defined in some old header
static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

class Split : public Module {
 public:
  struct snobj *Init(struct snobj *arg);

  void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const std::vector<struct Command> cmds;

 private:
  uint64_t mask_;
  int attr_id_;
  int offset_;
  int size_;
};

const std::vector<struct Command> Split::cmds = {};

struct snobj *Split::Init(struct snobj *arg) {
  if (!arg || snobj_type(arg) != TYPE_MAP)
    return snobj_err(EINVAL, "specify 'offset'/'name' and 'size'");

  size_ = snobj_eval_uint(arg, "size");
  if (size_ < 1 || size_ > MAX_SIZE)
    return snobj_err(EINVAL, "'size' must be 1-%d", MAX_SIZE);

  mask_ = ((uint64_t)1 << (size_ * 8)) - 1;

  const char *name = snobj_eval_str(arg, "name");

  if (name) {
    attr_id_ = add_metadata_attr(this, name, size_, MT_READ);
    if (attr_id_ < 0)
      return snobj_err(-attr_id_, "add_metadata_attr() failed");
  } else if (snobj_eval_exists(arg, "offset")) {
    attr_id_ = -1;
    offset_ = snobj_eval_int(arg, "offset");
    if (offset_ < 0 || offset_ > 1024)
      return snobj_err(EINVAL, "invalid 'offset'");
    offset_ -= (8 - size_);
  } else
    return snobj_err(EINVAL, "must specify 'offset' or 'name'");

  return NULL;
}

void Split::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t ogate[MAX_PKT_BURST];
  int cnt = batch->cnt;

  if (attr_id_ >= 0) {
    int attr_id = attr_id_;

    for (int i = 0; i < cnt; i++) {
      struct snbuf *pkt = batch->pkts[i];

      uint64_t val = get_attr(this, attr_id, pkt, uint64_t);
      val &= mask_;

      if (is_valid_gate(val))
        ogate[i] = val;
      else
        ogate[i] = DROP_GATE;
    }
  } else {
    int offset = offset_;

    for (int i = 0; i < cnt; i++) {
      struct snbuf *pkt = batch->pkts[i];
      char *head = static_cast<char *>(snb_head_data(pkt));

      uint64_t val = *(uint64_t *)(head + offset);
      val = rte_be_to_cpu_64(val) & mask_;

      if (is_valid_gate(val))
        ogate[i] = val;
      else
        ogate[i] = DROP_GATE;
    }
  }

  run_split(this, ogate, batch);
}

ADD_MODULE(Split, "split", "split packets depending on packet data or metadata attributes")
