#ifndef BESS_MODULES_VLANPUSH_H_
#define BESS_MODULES_VLANPUSH_H_

#include "../module.h"
#include "../module_msg.pb.h"

class VLANPush : public Module {
 public:
  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

  VLANPush() : Module(), vlan_tag_(), qinq_tag_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::VLANPushArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc() const;

  struct snobj *CommandSetTci(struct snobj *arg);
  pb_cmd_response_t CommandSetTciPb(const bess::pb::VLANPushArg &arg);

 private:
  /* network order */
  uint32_t vlan_tag_;
  uint32_t qinq_tag_;
};

#endif  // BESS_MODULES_VLANPUSH_H_
