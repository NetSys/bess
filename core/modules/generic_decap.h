#ifndef BESS_MODULES_GENERICDECAP_H_
#define BESS_MODULES_GENERICDECAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class GenericDecap final : public Module {
 public:
  GenericDecap() : Module(), decap_size_() {}

  CommandResponse Init(const bess::pb::GenericDecapArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

 private:
  int decap_size_;
};

#endif  // BESS_MODULES_GENERICDECAP_H_
