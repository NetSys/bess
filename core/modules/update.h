#ifndef __UPDATE_H__
#define __UPDATE_H__

#include "../module.h"

#define UPDATE_MAX_FIELDS 16

class Update : public Module {
 public:
  Update() : Module(), num_fields_(), fields_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::protobuf::UpdateArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  pb_error_t CommandAdd(const bess::protobuf::UpdateArg &arg);
  pb_error_t CommandClear(const bess::protobuf::UpdateCommandClearArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  int num_fields_ = {};

  struct {
    uint64_t mask;  /* bits with 1 won't be updated */
    uint64_t value; /* in network order */
    int16_t offset;
  } fields_[UPDATE_MAX_FIELDS];
};

#endif
