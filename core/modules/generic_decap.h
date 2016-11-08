#ifndef BESS_MODULES_GENERICDECAP_H_
#define BESS_MODULES_GENERICDECAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class GenericDecap : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  GenericDecap() : Module(), decap_size_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::GenericDecapArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  int decap_size_;
};

#endif  // BESS_MODULES_GENERICDECAP_H_
