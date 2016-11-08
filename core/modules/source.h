#ifndef BESS_MODULES_FLOWGEN_H_
#define BESS_MODULES_FLOWGEN_H_

#include "../module.h"
#include "../module_msg.pb.h"

class Source : public Module {
 public:
  Source() : Module(), pkt_size_(), burst_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::SourceArg &arg);

  virtual struct task_result RunTask(void *arg);

  struct snobj *command_set_pkt_size(struct snobj *arg);
  struct snobj *command_set_burst(struct snobj *arg);

  pb_cmd_response_t CommandSetBurstPb(
      const bess::pb::SourceCommandSetBurstArg &arg);
  pb_cmd_response_t CommandSetPktSizePb(
      const bess::pb::SourceCommandSetPktSizeArg &arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  int pkt_size_;
  int burst_;
};

#endif  // BESS_MODULES_FLOWGEN_H_
