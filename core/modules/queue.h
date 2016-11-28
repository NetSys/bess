#ifndef BESS_MODULES_QUEUE_H_
#define BESS_MODULES_QUEUE_H_

#include "../kmod/llring.h"
#include "../module.h"
#include "../module_msg.pb.h"

class Queue final : public Module {
 public:
  static const PbCommands pb_cmds;

  Queue() : Module(), queue_(), prefetch_(), burst_() {}

  pb_error_t InitPb(const bess::pb::QueueArg &arg);

  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);
  virtual void ProcessBatch(bess::PacketBatch *batch);

  virtual std::string GetDesc() const;

  pb_cmd_response_t CommandSetBurstPb(
      const bess::pb::QueueCommandSetBurstArg &arg);
  pb_cmd_response_t CommandSetSizePb(
      const bess::pb::QueueCommandSetSizeArg &arg);

 private:
  int Resize(int slots);
  pb_error_t SetBurst(int64_t burst);
  pb_error_t SetSize(uint64_t size);
  struct llring *queue_ = {};
  int prefetch_ = {};
  int burst_ = {};
};

#endif  // BESS_MODULES_QUEUE_H_
