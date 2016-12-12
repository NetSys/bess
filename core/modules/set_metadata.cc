#include "set_metadata.h"
#include "../utils/endian.h"

static void CopyFromPacket(bess::PacketBatch *batch, const struct Attr *attr,
                           bess::metadata::mt_offset_t mt_off) {
  int cnt = batch->cnt();
  int size = attr->size;

  int pkt_off = attr->offset;

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    char *head = pkt->head_data<char *>();
    void *mt_ptr;

    mt_ptr = _ptr_attr_with_offset<value_t>(mt_off, pkt);
    rte_memcpy(mt_ptr, head + pkt_off, size);
  }
}

static void CopyFromValue(bess::PacketBatch *batch, const struct Attr *attr,
                          bess::metadata::mt_offset_t mt_off) {
  int cnt = batch->cnt();
  int size = attr->size;

  const void *val_ptr = &attr->value;

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    void *mt_ptr;

    mt_ptr = _ptr_attr_with_offset<value_t>(mt_off, pkt);
    rte_memcpy(mt_ptr, val_ptr, size);
  }
}

pb_error_t SetMetadata::AddAttrOne(
    const bess::pb::SetMetadataArg_Attribute &attr) {
  std::string name;
  size_t size = 0;
  int offset = -1;
  value_t value = value_t();

  int ret;

  if (!attr.name().length()) {
    return pb_error(EINVAL, "'name' field is missing");
  }
  name = attr.name();
  size = attr.size();

  if (size < 1 || size > bess::metadata::kMetadataAttrMaxSize) {
    return pb_error(EINVAL, "'size' must be 1-%zu",
                    bess::metadata::kMetadataAttrMaxSize);
  }

  if (attr.value() != 0) {
    if (uint64_to_bin((uint8_t *)&value, size, attr.value(),
                      bess::utils::is_be_system())) {
      return pb_error(EINVAL,
                      "'value' field has not a "
                      "correct %lu-byte value",
                      size);
    }
  } else {
    offset = attr.offset();
    if (offset < 0 || offset + size >= SNBUF_DATA) {
      return pb_error(EINVAL, "invalid packet offset");
    }
  }

  ret = AddMetadataAttr(name, size,
                        bess::metadata::Attribute::AccessMode::kWrite);
  if (ret < 0)
    return pb_error(-ret, "add_metadata_attr() failed");

  attrs_.emplace_back();
  attrs_.back().name = name;
  attrs_.back().size = size;
  attrs_.back().offset = offset;
  attrs_.back().value = value;

  return pb_errno(0);
}

pb_error_t SetMetadata::Init(const bess::pb::SetMetadataArg &arg) {
  if (!arg.attrs_size()) {
    return pb_error(EINVAL, "'attrs' must be specified");
  }

  for (int i = 0; i < arg.attrs_size(); i++) {
    const auto &attr = arg.attrs(i);
    pb_error_t err;

    err = AddAttrOne(attr);
    if (err.err() != 0) {
      return err;
    }
  }

  return pb_errno(0);
}

void SetMetadata::ProcessBatch(bess::PacketBatch *batch) {
  for (size_t i = 0; i < attrs_.size(); i++) {
    const struct Attr *attr = &attrs_[i];

    bess::metadata::mt_offset_t mt_offset = attr_offset(i);

    if (!bess::metadata::IsValidOffset(mt_offset)) {
      continue;
    }

    /* copy data from the packet */
    if (attr->offset >= 0) {
      CopyFromPacket(batch, attr, mt_offset);
    } else {
      CopyFromValue(batch, attr, mt_offset);
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(SetMetadata, "setattr", "Set metadata attributes to packets")
