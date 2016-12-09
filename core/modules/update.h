#ifndef BESS_MODULES_UPDATE_H_
#define BESS_MODULES_UPDATE_H_

#include "../module.h"
#include "../module_msg.pb.h"

#define UPDATE_MAX_FIELDS 16

class Update final : public Module {
 public:
  static const Commands cmds;

  Update() : Module(), num_fields_(), fields_() {}

  pb_error_t Init(const bess::pb::UpdateArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandAdd(const bess::pb::UpdateArg &arg);
  pb_cmd_response_t CommandClear(const bess::pb::EmptyArg &arg);

 private:
  int num_fields_;

  struct {
    uint64_t mask;  /* bits with 1 won't be updated */
    uint64_t value; /* in network order */
    int16_t offset;
  } fields_[UPDATE_MAX_FIELDS];
};

#endif  // BESS_MODULES_UPDATE_H_
