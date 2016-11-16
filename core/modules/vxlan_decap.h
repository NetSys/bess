#ifndef BESS_MODULES_VXLANDECAP_H_
#define BESS_MODULES_VXLANDECAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class VXLANDecap : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::VXLANDecapArg &arg);

  void ProcessBatch(struct pkt_batch *batch);
};

#endif  // BESS_MODULES_VXLANDECAP_H_
