#ifndef __MTTEST_H__
#define __MTTEST_H__

#include "../module.h"
#include "../module_msg.pb.h"

class MetadataTest : public Module {
 public:
  struct snobj *Init(struct snobj *arg);
  pb_error_t Init(const bess::pb::MetadataTestArg &arg);

  void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  struct snobj *AddAttributes(struct snobj *attrs,
                              bess::metadata::AccessMode mode);
  pb_error_t AddAttributes(
      const google::protobuf::Map<std::string, int64_t> &attrs,
      bess::metadata::AccessMode mode);
};

#endif
