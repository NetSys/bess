#ifndef BESS_MODULES_RANDOMUPDATE_H_
#define BESS_MODULES_RANDOMUPDATE_H_

#include "../module.h"
#include "../module_msg.pb.h"

#include "../utils/endian.h"
#include "../utils/random.h"

#define MAX_VARS 16

class RandomUpdate final : public Module {
 public:
  static const Commands cmds;

  RandomUpdate() : Module(), num_vars_(), vars_(), rng_() {}

  CommandResponse Init(const bess::pb::RandomUpdateArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::RandomUpdateArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  int num_vars_;

  struct {
    bess::utils::be32_t mask;  // bits with 1 won't be updated
    uint32_t min;
    uint32_t range;  // max - min + 1
    size_t offset;
    size_t bit_shift;
  } vars_[MAX_VARS];

  Random rng_;
};

#endif  // BESS_MODULES_RANDOMUPDATE_H_
