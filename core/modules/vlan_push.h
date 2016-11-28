#ifndef BESS_MODULES_VLANPUSH_H_
#define BESS_MODULES_VLANPUSH_H_

#include "../module.h"
#include "../module_msg.pb.h"

class VLANPush final : public Module {
 public:
  static const PbCommands pb_cmds;

  VLANPush() : Module(), vlan_tag_(), qinq_tag_() {}

  pb_error_t InitPb(const bess::pb::VLANPushArg &arg);

  virtual void ProcessBatch(bess::PacketBatch *batch);

  virtual std::string GetDesc() const;

  pb_cmd_response_t CommandSetTciPb(const bess::pb::VLANPushArg &arg);

 private:
  /* network order */
  uint32_t vlan_tag_;
  uint32_t qinq_tag_;
};

#endif  // BESS_MODULES_VLANPUSH_H_
