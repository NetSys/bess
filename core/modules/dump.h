#ifndef BESS_MODULES_DUMP_H_
#define BESS_MODULES_DUMP_H_

#include "../module.h"
#include "../module_msg.pb.h"

class Dump final : public Module {
 public:
  pb_error_t InitPb(const bess::pb::DumpArg &arg);

  virtual void ProcessBatch(bess::PacketBatch *batch);

  pb_cmd_response_t CommandSetIntervalPb(const bess::pb::DumpArg &arg);

  
  static const PbCommands pb_cmds;

 private:
  uint64_t min_interval_ns_;
  uint64_t next_ns_;
};

#endif  // BESS_MODULES_DUMP_H_
