#ifndef BESS_MODULES_ETHERENCAP_H_
#define BESS_MODULES_ETHERENCAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class EtherEncap final : public Module {
 public:
  CommandResponse Init(const bess::pb::EtherEncapArg &arg);

  void ProcessBatch(bess::PacketBatch *batch);
};

#endif  // BESS_MODULES_ETHERENCAP_H_
