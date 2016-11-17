#ifndef BESS_MODULES_SETMETADATA_H_
#define BESS_MODULES_SETMETADATA_H_

#include "../module.h"
#include "../module_msg.pb.h"

typedef struct { char bytes[bess::metadata::kMetadataAttrMaxSize]; } value_t;
INSTANTIATE_MT_FOR_TYPE(value_t)

struct Attr {
  std::string name;
  value_t value;
  int offset;
  int size;
};

class SetMetadata : public Module {
 public:
  SetMetadata() : Module(), attrs_() {}

  struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::SetMetadataArg &arg);

  void ProcessBatch(struct pkt_batch *batch);

 private:
  struct snobj *AddAttrOne(struct snobj *attr);
  pb_error_t AddAttrOne(const bess::pb::SetMetadataArg_Attribute &attr);

  std::vector<struct Attr> attrs_;
};

#endif  // BESS_MODULES_SETMETADATA_H_
