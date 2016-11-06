#ifndef BESS_MODULES_SPLIT_H_
#define BESS_MODULES_SPLIT_H_

#include "../module.h"

class Split : public Module {
 public:
  Split() : Module(), mask_(), attr_id_(), offset_(), size_() {}

  struct snobj *Init(struct snobj *arg);
  pb_error_t Init(const google::protobuf::Any &arg);

  void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  uint64_t mask_;
  int attr_id_;
  int offset_;
  int size_;
};

#endif  // BESS_MODULES_SPLIT_H_
