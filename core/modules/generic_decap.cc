#include "../module.h"

class GenericDecap : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const std::vector<struct Command> cmds;

 private:
  int decap_size_ = {0};
};

const std::vector<struct Command> GenericDecap::cmds = {};

struct snobj *GenericDecap::Init(struct snobj *arg) {
  if (!arg) return NULL;

  if (snobj_type(arg) == TYPE_INT)
    this->decap_size_ = snobj_uint_get(arg);
  else if (snobj_type(arg) == TYPE_MAP && snobj_eval_exists(arg, "bytes"))
    this->decap_size_ = snobj_eval_uint(arg, "bytes");
  else
    return snobj_err(EINVAL, "invalid argument");

  if (this->decap_size_ <= 0 || this->decap_size_ > 1024)
    return snobj_err(EINVAL, "invalid decap size");

  return NULL;
}

void GenericDecap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  int decap_size = this->decap_size_;

  for (int i = 0; i < cnt; i++) snb_adj(batch->pkts[i], decap_size);

  run_next_module(this, batch);
}

ADD_MODULE(GenericDecap, "generic_decap",
           "remove specified bytes from the beginning of packets")
