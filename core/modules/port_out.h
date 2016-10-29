#ifndef __PORT_OUT_H__
#define __PORT_OUT_H__

#include "../module.h"
#include "../port.h"

class PortOut : public Module {
 public:
  PortOut() : Module(), port_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::protobuf::PortOutArg &arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc();

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 0;

  static const Commands<Module> cmds;

 private:
  Port *port_;
};

#endif
