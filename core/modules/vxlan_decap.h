#ifndef BESS_MODULES_VXLANDECAP_H_
#define BESS_MODULES_VXLANDECAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class VXLANDecap final : public Module {
 public:
  CommandResponse Init(const bess::pb::VXLANDecapArg &arg);

  void ProcessBatch(bess::PacketBatch *batch);
};

#endif  // BESS_MODULES_VXLANDECAP_H_
