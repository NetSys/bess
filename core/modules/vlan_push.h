#ifndef BESS_MODULES_VLANPUSH_H_
#define BESS_MODULES_VLANPUSH_H_

#include "../module.h"
#include "../module_msg.pb.h"

class VLANPush final : public Module {
 public:
  static const Commands cmds;

  VLANPush() : Module(), vlan_tag_(), qinq_tag_() {}

  pb_error_t Init(const bess::pb::VLANPushArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

  pb_cmd_response_t CommandSetTci(const bess::pb::VLANPushArg &arg);

 private:
  /* network order */
  uint32_t vlan_tag_;
  uint32_t qinq_tag_;
};

#endif  // BESS_MODULES_VLANPUSH_H_
