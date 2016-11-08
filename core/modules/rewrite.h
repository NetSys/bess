#ifndef BESS_MODULES_REWRITE_H_
#define BESS_MODULES_REWRITE_H_

#include "../module.h"
#include "../module_msg.pb.h"

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
  pb_error_t InitPb(const bess::pb::RewriteArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  bess::pb::ModuleCommandResponse CommandAddPb(const bess::pb::RewriteArg &arg);
  bess::pb::ModuleCommandResponse CommandClearPb(const bess::pb::EmptyArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

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

#endif  // BESS_MODULES_REWRITE_H_
