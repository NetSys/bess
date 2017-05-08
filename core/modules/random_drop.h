#ifndef BESS_MODULES_RANDOM_DROP_H_
#define BESS_MODULES_RANDOM_DROP_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/random.h"

// RandomDrop drops packets with a specified probability [0, 1].
class RandomDrop final : public Module {
 public:
  static const uint32_t kRange = 1000000;  // for granularity
  CommandResponse Init(const bess::pb::RandomDropArg &arg);
  void ProcessBatch(bess::PacketBatch *batch) override;

 private:
  Random rng_;      // Random number generator
  uint threshold_;  // Drop threshold for random number generated
};

#endif  // BESS_MODULES_RANDOM_DROP_H_
