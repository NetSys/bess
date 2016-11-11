#ifndef BESS_MODULES_VXLANENCAP_H_
#define BESS_MODULES_VXLANENCAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

using bess::metadata::Attribute;

class VXLANEncap : public Module {
 public:
  VXLANEncap() : Module(), dstport_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::VXLANEncapArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  uint16_t dstport_;
};

#endif  // BESS_MODULES_VXLANENCAP_H_
