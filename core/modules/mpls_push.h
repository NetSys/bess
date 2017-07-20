#ifndef BESS_MODULES_MPLSPUSH_H_
#define BESS_MODULES_MPLSPUSH_H_

#include "../module.h"
#include "../module_msg.pb.h"

class MPLSPush final : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands cmds;

  MPLSPush();  // constructor

  CommandResponse Init(const bess::pb::MplsPushArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandSet(const bess::pb::MplsPushArg &arg);

 private:
  uint32_t label_;
  uint8_t ttl_;
  uint8_t tc_;
  bool is_bottom_of_stack_;
};

#endif  // BESS_MODULES_MPLSPUSH_H_
