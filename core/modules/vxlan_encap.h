#ifndef BESS_MODULES_VXLANENCAP_H_
#define BESS_MODULES_VXLANENCAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

#include "../utils/endian.h"

class VXLANEncap final : public Module {
 public:
  static const uint16_t kDefaultDstPort;

  VXLANEncap() : Module(), dstport_() {}

  CommandResponse Init(const bess::pb::VXLANEncapArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

 private:
  bess::utils::be16_t dstport_;
};

#endif  // BESS_MODULES_VXLANENCAP_H_
