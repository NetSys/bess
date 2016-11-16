#ifndef BESS_MODULES_RANDOMUPDATE_H_
#define BESS_MODULES_RANDOMUPDATE_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/random.h"

#define MAX_VARS 16

class RandomUpdate : public Module {
 public:
  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

  RandomUpdate() : Module(), num_vars_(), vars_(), rng_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::RandomUpdateArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  pb_cmd_response_t CommandAddPb(
      const bess::pb::RandomUpdateArg &arg);
  pb_cmd_response_t CommandClearPb(const bess::pb::EmptyArg &arg);

 private:
  int num_vars_;

  struct {
    uint32_t mask; /* bits with 1 won't be updated */
    uint32_t min;
    uint32_t range; /* == max - min + 1 */
    int16_t offset;
  } vars_[MAX_VARS];

  Random rng_;
};

#endif  // BESS_MODULES_RANDOMUPDATE_H_
