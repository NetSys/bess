#ifndef __SPLIT_H__
#define __SPLIT_H__

#include "../module.h"
#include "../module_msg.pb.h"

class Split : public Module {
 public:
  Split() : Module(), mask_(), attr_id_(), offset_(), size_() {}

  struct snobj *Init(struct snobj *arg);
  pb_error_t Init(const bess::pb::SplitArg &arg);

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

#endif
