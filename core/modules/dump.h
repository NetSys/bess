#ifndef BESS_MODULES_DUMP_H_
#define BESS_MODULES_DUMP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class Dump final : public Module {
 public:
  pb_error_t Init(const bess::pb::DumpArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandSetInterval(const bess::pb::DumpArg &arg);

  static const Commands cmds;

 private:
  uint64_t min_interval_ns_;
  uint64_t next_ns_;
};

#endif  // BESS_MODULES_DUMP_H_
