#ifndef BESS_MODULES_QUEUEINC_H_
#define BESS_MODULES_QUEUEINC_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../port.h"

class QueueInc : public Module {
 public:
  static const gate_idx_t kNumIGates = 0;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

  QueueInc() : Module(), port_(), qid_(), prefetch_(), burst_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::QueueIncArg &arg);
  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);

  virtual std::string GetDesc() const;

  struct snobj *CommandSetBurst(struct snobj *arg);
  pb_cmd_response_t CommandSetBurstPb(
      const bess::pb::QueueIncCommandSetBurstArg &arg);

 private:
  Port *port_;
  queue_t qid_;
  int prefetch_;
  int burst_;
  pb_error_t SetBurst(int64_t burst);
};

#endif  // BESS_MODULES_QUEUEINC_H_
