#ifndef BESS_MODULES_VLANPUSH_H_
#define BESS_MODULES_VLANPUSH_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/endian.h"

using bess::utils::be32_t;

class VLANPush final : public Module {
 public:
  static const Commands cmds;

  VLANPush() : Module(), vlan_tag_(), qinq_tag_() {}

  CommandResponse Init(const bess::pb::VLANPushArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

  CommandResponse CommandSetTci(const bess::pb::VLANPushArg &arg);

 private:
  be32_t vlan_tag_;
  be32_t qinq_tag_;
};

#endif  // BESS_MODULES_VLANPUSH_H_
