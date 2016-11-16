#ifndef BESS_MODULES_MTTEST_H_
#define BESS_MODULES_MTTEST_H_

#include "../module.h"
#include "../module_msg.pb.h"

using bess::metadata::Attribute;

class MetadataTest : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::MetadataTestArg &arg);

  void ProcessBatch(struct pkt_batch *batch);

 private:
  struct snobj *AddAttributes(struct snobj *attrs,
                              Attribute::AccessMode mode);
  pb_error_t AddAttributes(
      const google::protobuf::Map<std::string, int64_t> &attrs,
      Attribute::AccessMode mode);
};

#endif  // BESS_MODULES_MTTEST_H_
