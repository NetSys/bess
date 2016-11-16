#ifndef BESS_MODULES_PORTOUT_H_
#define BESS_MODULES_PORTOUT_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../port.h"

class PortOut : public Module {
 public:
  static const gate_idx_t kNumOGates = 0;

  PortOut() : Module(), port_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::PortOutArg &arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc() const;

 private:
  Port *port_;
};

#endif  // BESS_MODULES_PORTOUT_H_
