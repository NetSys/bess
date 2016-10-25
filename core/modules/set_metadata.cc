#include "../module.h"

typedef struct { char bytes[MT_ATTR_MAX_SIZE]; } value_t;

struct Attr {
  std::string name;
  value_t value;
  int offset;
  int size;
};

static void CopyFromPacket(struct pkt_batch *batch, const struct Attr *attr,
                           mt_offset_t mt_off) {
  int cnt = batch->cnt;
  int size = attr->size;

  int pkt_off = attr->offset;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];
    char *head = static_cast<char *>(snb_head_data(pkt));
    void *mt_ptr;

    mt_ptr = _ptr_attr_with_offset(mt_off, pkt, value_t);
    rte_memcpy(mt_ptr, head + pkt_off, size);
  }
}

static void CopyFromValue(struct pkt_batch *batch, const struct Attr *attr,
                          mt_offset_t mt_off) {
  int cnt = batch->cnt;
  int size = attr->size;

  const void *val_ptr = &attr->value;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];
    void *mt_ptr;

    mt_ptr = _ptr_attr_with_offset(mt_off, pkt, value_t);
    rte_memcpy(mt_ptr, val_ptr, size);
  }
}

class SetMetadata : public Module {
 public:
  SetMetadata() : Module(), attrs_() {}

  struct snobj *Init(struct snobj *arg);

  void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  struct snobj *AddAttrOne(struct snobj *attr);

  std::vector<struct Attr> attrs_;
};

const Commands<Module> SetMetadata::cmds = {};

struct snobj *SetMetadata::AddAttrOne(struct snobj *attr) {
  const char *name_c;
  std::string name;
  int size = 0;
  int offset = -1;
  value_t value = {};

  struct snobj *t;

  int ret;

  if (attr->type != TYPE_MAP) {
    return snobj_err(EINVAL, "argument must be a map or a list of maps");
  }

  name_c = snobj_eval_str(attr, "name");
  if (!name_c) {
    return snobj_err(EINVAL, "'name' field is missing");
  }
  name = std::string(name_c);

  size = snobj_eval_uint(attr, "size");

  if (size < 1 || size > MT_ATTR_MAX_SIZE) {
    return snobj_err(EINVAL, "'size' must be 1-%d", MT_ATTR_MAX_SIZE);
  }

  if ((t = snobj_eval(attr, "value"))) {
    if (snobj_binvalue_get(t, size, &value, 0)) {
      return snobj_err(EINVAL,
                       "'value' field has not a "
                       "correct %d-byte value",
                       size);
    }
  } else if ((t = snobj_eval(attr, "offset"))) {
    if (snobj_type(t) != TYPE_INT) {
      return snobj_err(EINVAL, "'offset' must be an integer");
    }

    offset = snobj_int_get(t);
    if (offset < 0 || offset + size >= SNBUF_DATA) {
      return snobj_err(EINVAL, "invalid packet offset");
    }
  }

  ret = AddMetadataAttr(name, size, MT_WRITE);
  if (ret < 0)
    return snobj_err(-ret, "add_metadata_attr() failed");

  attrs_.emplace_back();
  attrs_.back().name = name;
  attrs_.back().size = size;
  attrs_.back().offset = offset;
  attrs_.back().value = value;

  return nullptr;
}

struct snobj *SetMetadata::Init(struct snobj *arg) {
  struct snobj *list;

  if (!arg || !(list = snobj_eval(arg, "attrs"))) {
    return snobj_err(EINVAL, "'attrs' must be specified");
  }

  if (snobj_type(list) != TYPE_LIST) {
    return snobj_err(EINVAL, "'attrs' must be a map or a list of maps");
  }

  for (size_t i = 0; i < list->size; i++) {
    struct snobj *attr = snobj_list_get(list, i);
    struct snobj *err;

    err = AddAttrOne(attr);
    if (err) {
      return err;
    }
  }

  return nullptr;
}

void SetMetadata::ProcessBatch(struct pkt_batch *batch) {
  for (size_t i = 0; i < attrs_.size(); i++) {
    const struct Attr *attr = &attrs_[i];

    mt_offset_t mt_offset = SetMetadata::attr_offsets[i];

    if (!is_valid_offset(mt_offset)) {
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
