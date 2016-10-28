#ifndef __REWRITE_H__
#define __REWRITE_H__

#include "../module.h"

#define SLOTS (MAX_PKT_BURST * 2 - 1)
#define MAX_TEMPLATE_SIZE 1536

class Rewrite : public Module {
 public:
  Rewrite()
      : Module(),
        next_turn_(),
        num_templates_(),
        template_size_(),
        templates_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::protobuf::RewriteArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  pb_error_t CommandAdd(const bess::protobuf::RewriteArg &arg);
  pb_error_t CommandClear(const bess::protobuf::RewriteCommandClearArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  inline void DoRewrite(struct pkt_batch *batch);
  inline void DoRewriteSingle(struct pkt_batch *batch);

  /* For fair round robin we remember the next index for later.
   * [0, num_templates - 1] */
  int next_turn_;

  int num_templates_;
  uint16_t template_size_[SLOTS];
  unsigned char templates_[SLOTS][MAX_TEMPLATE_SIZE] __ymm_aligned;
};

#endif
