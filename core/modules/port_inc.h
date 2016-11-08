#ifndef BESS_MODULES_PORTINC_H_
#define BESS_MODULES_PORTINC_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../port.h"

class PortInc : public Module {
 public:
  PortInc() : Module(), port_(), prefetch_(), burst_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::PortIncArg &arg);

  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);

  virtual std::string GetDesc() const;

  struct snobj *CommandSetBurst(struct snobj *arg);
  pb_cmd_response_t CommandSetBurstPb(
      const bess::pb::PortIncCommandSetBurstArg &arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  Port *port_ = {};
  int prefetch_ = {};
  int burst_ = {};
  pb_error_t SetBurst(int64_t burst);
};

#endif  // BESS_MODULES_PORTINC_H_
