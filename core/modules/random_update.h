#ifndef BESS_MODULES_RANDOMUPDATE_H_
#define BESS_MODULES_RANDOMUPDATE_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/random.h"

#define MAX_VARS 16

class RandomUpdate final : public Module {
 public:
  static const Commands cmds;

  RandomUpdate() : Module(), num_vars_(), vars_(), rng_() {}

  pb_error_t Init(const bess::pb::RandomUpdateArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandAdd(const bess::pb::RandomUpdateArg &arg);
  pb_cmd_response_t CommandClear(const bess::pb::EmptyArg &arg);

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
