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

  this->size_ = snobj_eval_uint(arg, "size");
  if (this->size_ < 1 || this->size_ > MAX_SIZE)
    return snobj_err(EINVAL, "'size' must be 1-%d", MAX_SIZE);

  this->mask_ = ((uint64_t)1 << (this->size_ * 8)) - 1;

  const char *name = snobj_eval_str(arg, "name");

  if (name) {
    this->attr_id_ = add_metadata_attr(this, name, this->size_, MT_READ);
    if (this->attr_id_ < 0)
      return snobj_err(-this->attr_id_, "add_metadata_attr() failed");
  } else if (snobj_eval_exists(arg, "offset")) {
    this->attr_id_ = -1;
    this->offset_ = snobj_eval_int(arg, "offset");
    if (this->offset_ < 0 || this->offset_ > 1024)
      return snobj_err(EINVAL, "invalid 'offset'");
    this->offset_ -= (8 - this->size_);
  } else
    return snobj_err(EINVAL, "must specify 'offset' or 'name'");

  return NULL;
}

void Split::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t ogate[MAX_PKT_BURST];
  int cnt = batch->cnt;

  if (this->attr_id_ >= 0) {
    int attr_id = this->attr_id_;

    for (int i = 0; i < cnt; i++) {
      struct snbuf *pkt = batch->pkts[i];

      uint64_t val = get_attr(this, attr_id, pkt, uint64_t);
      val &= this->mask_;

      if (is_valid_gate(val))
        ogate[i] = val;
      else
        ogate[i] = DROP_GATE;
    }
  } else {
    int offset = this->offset_;

    for (int i = 0; i < cnt; i++) {
      struct snbuf *pkt = batch->pkts[i];
      char *head = static_cast<char *>(snb_head_data(pkt));

      uint64_t val = *(uint64_t *)(head + offset);
      val = rte_be_to_cpu_64(val) & this->mask_;

      if (is_valid_gate(val))
        ogate[i] = val;
      else
        ogate[i] = DROP_GATE;
    }
  }

  run_split(this, ogate, batch);
}

ADD_MODULE(Split, "split", "split packets depending on packet data or metadata attributes")
