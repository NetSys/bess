#ifndef BESS_MODULES_SPLIT_H_
#define BESS_MODULES_SPLIT_H_

#include "../module.h"
#include "../module_msg.pb.h"

class Split : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  Split() : Module(), mask_(), attr_id_(), offset_(), size_() {}

  struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::SplitArg &arg);

  void ProcessBatch(struct pkt_batch *batch);

 private:
  uint64_t mask_;
  int attr_id_;
  int offset_;
  int size_;
};

#endif  // BESS_MODULES_SPLIT_H_
