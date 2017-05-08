#include "generic_encap.h"

#include "../utils/endian.h"

static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define MAX_HEADER_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

CommandResponse GenericEncap::AddFieldOne(
    const bess::pb::GenericEncapArg_Field &field, struct Field *f, int idx) {
  f->size = field.size();
  if (f->size < 1 || f->size > MAX_FIELD_SIZE) {
    return CommandFailure(EINVAL, "idx %d: 'size' must be 1-%d", idx,
                          MAX_FIELD_SIZE);
  }

  if (field.insertion_case() == bess::pb::GenericEncapArg_Field::kAttribute) {
    const char *attr = field.attribute().c_str();
    f->attr_id = AddMetadataAttr(attr, f->size,
                                 bess::metadata::Attribute::AccessMode::kRead);
    if (f->attr_id < 0) {
      return CommandFailure(-f->attr_id, "idx %d: add_metadata_attr() failed",
                            idx);
    }
  } else if (field.insertion_case() ==
             bess::pb::GenericEncapArg_Field::kValue) {
    f->attr_id = -1;
    if (!bess::utils::uint64_to_bin(&f->value, field.value(), f->size, 1)) {
      return CommandFailure(EINVAL,
                            "idx %d: "
                            "not a correct %d-byte value",
                            idx, f->size);
    }
  } else {
    return CommandFailure(EINVAL, "idx %d: must specify 'value' or 'attribute'",
                          idx);
  }

  return CommandSuccess();
}

/* Takes a list of fields. Each field is either:
 *
 *  1. {'size': X, 'value': Y}		(for constant values)
 *  2. {'size': X, 'attribute': Y}	(for metadata attributes)
 *
 * e.g.: GenericEncap([{'size': 4, 'value': 0xdeadbeef},
 *                     {'size': 2, 'attribute': 'foo'},
 *                     {'size': 2, 'value': 0x1234}])
 * will prepend a 8-byte header:
 *    de ad be ef <xx> <xx> 12 34
 * where the 2-byte <xx> <xx> comes from the value of metadata attribute 'foo'
 * for each packet.
 */
CommandResponse GenericEncap::Init(const bess::pb::GenericEncapArg &arg) {
  int size_acc = 0;

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &field = arg.fields(i);
    CommandResponse err;
    struct Field *f = &fields_[i];

    f->pos = size_acc;

    err = AddFieldOne(field, f, i);
    if (err.error().code() != 0) {
      return err;
    }

    size_acc += f->size;
  }

  encap_size_ = size_acc;
  num_fields_ = arg.fields_size();

  return CommandSuccess();
}

void GenericEncap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  int encap_size = encap_size_;

  char headers[bess::PacketBatch::kMaxBurst][MAX_HEADER_SIZE] __ymm_aligned;

  for (int i = 0; i < num_fields_; i++) {
    uint64_t value = fields_[i].value;

    int attr_id = fields_[i].attr_id;
    int offset = (attr_id >= 0) ? attr_offset(attr_id) : 0;

    char *header = headers[0] + fields_[i].pos;

    for (int j = 0; j < cnt; j++, header += MAX_HEADER_SIZE) {
      bess::Packet *pkt = batch->pkts()[j];
      *(reinterpret_cast<uint64_t *>(header)) =
          (attr_id < 0) ? value : get_attr_with_offset<uint64_t>(offset, pkt);
    }
  }

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    char *p = static_cast<char *>(pkt->prepend(encap_size));

    if (unlikely(!p)) {
      continue;
    }

    bess::utils::CopyInlined(p, headers[i], encap_size);
  }

  RunNextModule(batch);
}

ADD_MODULE(GenericEncap, "generic_encap",
           "encapsulates packets with constant values and metadata attributes")
