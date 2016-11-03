#ifndef __RANDOM_UPDATE_H__
#define __RANDOM_UPDATE_H__

#include "../module.h"
#include "../utils/random.h"

#define MAX_VARS 16

class RandomUpdate : public Module {
 public:
  RandomUpdate() : Module(), num_vars_(), vars_(), rng_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::protobuf::RandomUpdateArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  pb_error_t CommandAdd(const bess::protobuf::RandomUpdateArg &arg);
  pb_error_t CommandClear(
      const bess::protobuf::RandomUpdateCommandClearArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  int num_vars_ = {};

  struct {
    uint32_t mask; /* bits with 1 won't be updated */
    uint32_t min;
    uint32_t range; /* == max - min + 1 */
    int16_t offset;
  } vars_[MAX_VARS];

  Random rng_;
};

#endif
