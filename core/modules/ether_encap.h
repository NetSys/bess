#ifndef BESS_MODULES_ETHERENCAP_H_
#define BESS_MODULES_ETHERENCAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class EtherEncap : public Module {
 public:
  struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::EtherEncapArg &arg);

  void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

#endif  // BESS_MODULES_ETHERENCAP_H_
