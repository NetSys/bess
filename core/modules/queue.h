#ifndef BESS_MODULES_QUEUE_H_
#define BESS_MODULES_QUEUE_H_

#include "../kmod/llring.h"
#include "../module.h"
#include "../module_msg.pb.h"

class Queue : public Module {
 public:
  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

  Queue() : Module(), queue_(), prefetch_(), burst_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::QueueArg &arg);

  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc() const;

  struct snobj *CommandSetBurst(struct snobj *arg);
  struct snobj *CommandSetSize(struct snobj *arg);

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
