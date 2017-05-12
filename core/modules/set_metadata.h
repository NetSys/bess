#ifndef BESS_MODULES_SETMETADATA_H_
#define BESS_MODULES_SETMETADATA_H_

#include "../module.h"
#include "../module_msg.pb.h"

typedef struct { char bytes[bess::metadata::kMetadataAttrMaxSize]; } value_t;

struct Attr {
  std::string name;
  value_t value;
  int offset;
  int size;
};

class SetMetadata final : public Module {
 public:
  SetMetadata() : Module(), attrs_() {}

  CommandResponse Init(const bess::pb::SetMetadataArg &arg);

  void ProcessBatch(bess::PacketBatch *batch);

 private:
  CommandResponse AddAttrOne(const bess::pb::SetMetadataArg_Attribute &attr);

  std::vector<struct Attr> attrs_;
};

#endif  // BESS_MODULES_SETMETADATA_H_
