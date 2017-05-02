#ifndef BESS_MODULES_MTTEST_H_
#define BESS_MODULES_MTTEST_H_

#include "../module.h"
#include "../module_msg.pb.h"

using bess::metadata::Attribute;

class MetadataTest final : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  CommandResponse Init(const bess::pb::MetadataTestArg &arg);

  void ProcessBatch(bess::PacketBatch *batch);

 private:
  CommandResponse AddAttributes(
      const google::protobuf::Map<std::string, int64_t> &attrs,
      Attribute::AccessMode mode);
};

#endif  // BESS_MODULES_MTTEST_H_
