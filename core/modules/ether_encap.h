#ifndef BESS_MODULES_ETHERENCAP_H_
#define BESS_MODULES_ETHERENCAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class EtherEncap : public Module {
 public:
  struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::EtherEncapArg &arg);

  void ProcessBatch(struct pkt_batch *batch);
};

#endif  // BESS_MODULES_ETHERENCAP_H_
