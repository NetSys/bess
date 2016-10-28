#ifndef __MTTEST_H__
#define __MTTEST_H__

#include "../module.h"

class MetadataTest : public Module {
 public:
  struct snobj *Init(struct snobj *arg);
  pb_error_t Init(const bess::protobuf::MetadataTestArg &arg);

  void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;

 private:
  struct snobj *AddAttributes(struct snobj *attrs, enum mt_access_mode mode);
  pb_error_t AddAttributes(
      const google::protobuf::Map<std::string, int64_t> &attrs,
      enum mt_access_mode mode);
};

#endif
