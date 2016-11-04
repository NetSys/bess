#ifndef __QUEUE_OUT_H__
#define __QUEUE_OUT_H__

#include "../module.h"
#include "../port.h"

class QueueOut : public Module {
 public:
  QueueOut() : Module(), port_(), qid_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const google::protobuf::Any &arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc() const;

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 0;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  Port *port_;
  queue_t qid_;
};

#endif
