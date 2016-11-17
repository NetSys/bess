#ifndef BESS_MODULES_IPENCAP_H_
#define BESS_MODULES_IPENCAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class IPEncap : public Module {
 public:
  struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::IPEncapArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);
};

#endif  // BESS_MODULES_IPENCAP_H_
