#ifndef BESS_MODULES_IPENCAP_H_
#define BESS_MODULES_IPENCAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class IPEncap final : public Module {
 public:
  CommandResponse Init(const bess::pb::IPEncapArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;
};

#endif  // BESS_MODULES_IPENCAP_H_
