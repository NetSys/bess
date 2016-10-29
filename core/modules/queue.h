#ifndef __QUEUE_H__
#define __QUEUE_H__

#include "../kmod/llring.h"
#include "../module.h"

class Queue : public Module {
 public:
  Queue() : Module(), queue_(), prefetch_(), burst_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::protobuf::QueueArg &arg);

  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc() const;

  struct snobj *CommandSetBurst(struct snobj *arg);
  struct snobj *CommandSetSize(struct snobj *arg);

  pb_error_t CommandSetBurst(
      const bess::protobuf::QueueCommandSetBurstArg &arg);
  pb_error_t CommandSetSize(const bess::protobuf::QueueCommandSetSizeArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  int Resize(int slots);
  pb_error_t SetBurst(int64_t burst);
  pb_error_t SetSize(uint64_t size);
  struct llring *queue_ = {};
  int prefetch_ = {};
  int burst_ = {};
};

#endif
