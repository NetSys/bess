#ifndef BESS_MODULES_GENERICDECAP_H_
#define BESS_MODULES_GENERICDECAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class GenericDecap : public Module {
 public:
  GenericDecap() : Module(), decap_size_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::GenericDecapArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

 private:
  int decap_size_;
};

#endif  // BESS_MODULES_GENERICDECAP_H_
