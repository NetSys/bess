#ifndef BESS_MODULES_GENERICENCAP_H_
#define BESS_MODULES_GENERICENCAP_H_

#include "../module.h"
#include "../module_msg.pb.h"

#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8

struct Field {
  uint64_t value; /* onlt for constant values */
  int attr_id;    /* -1 for constant values */
  int pos;        /* relative position in the new header */
  int size;       /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

class GenericEncap final : public Module {
 public:
  GenericEncap() : Module(), encap_size_(), num_fields_(), fields_() {}

  pb_error_t Init(const bess::pb::GenericEncapArg &arg);

  void ProcessBatch(bess::PacketBatch *batch);

 private:
  pb_error_t AddFieldOne(const bess::pb::GenericEncapArg_Field &field,
                         struct Field *f, int idx);

  int encap_size_;

  int num_fields_;

  struct Field fields_[MAX_FIELDS];
};

#endif  // BESS_MODULES_GENERICENCAP_H_
