#ifndef __FLOWGEN_H__
#define __FLOWGEN_H__

#include "../module.h"

class Source : public Module {
 public:
  Source() : Module(), pkt_size_(), burst_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t Init(const bess::protobuf::SourceArg &arg);

  virtual struct task_result RunTask(void *arg);

  struct snobj *command_set_pkt_size(struct snobj *arg);
  struct snobj *command_set_burst(struct snobj *arg);

  pb_error_t CommandSetBurst(
      const bess::protobuf::SourceCommandSetBurstArg &arg);
  pb_error_t CommandSetPktSize(
      const bess::protobuf::SourceCommandSetPktSizeArg &arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  int pkt_size_;
  int burst_;
};

#endif
