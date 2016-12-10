#ifndef BESS_MODULES_PORTINC_H_
#define BESS_MODULES_PORTINC_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../port.h"

class PortInc final : public Module {
 public:
  static const gate_idx_t kNumIGates = 0;

  static const Commands cmds;

  PortInc() : Module(), port_(), prefetch_(), burst_() {}

  pb_error_t Init(const bess::pb::PortIncArg &arg);

  void DeInit() override;

  struct task_result RunTask(void *arg) override;

  std::string GetDesc() const override;

  pb_cmd_response_t CommandSetBurst(
      const bess::pb::PortIncCommandSetBurstArg &arg);

 private:
  pb_error_t SetBurst(int64_t burst);

  Port *port_;
  int prefetch_;
  int burst_;
};

#endif  // BESS_MODULES_PORTINC_H_
