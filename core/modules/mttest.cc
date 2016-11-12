#include "mttest.h"

#include <glog/logging.h>

const Commands<Module> MetadataTest::cmds = {};
const PbCommands MetadataTest::pb_cmds = {};

pb_error_t MetadataTest::AddAttributes(
    const google::protobuf::Map<std::string, int64_t> &attributes,
    Attribute::AccessMode mode) {
  for (const auto &kv : attributes) {
    int ret;

    const char *attr_name = kv.first.c_str();
    int attr_size = kv.second;

    ret = AddMetadataAttr(attr_name, attr_size, mode);
    if (ret < 0)
      return pb_error(-ret, "invalid metadata declaration");

    /* check /var/log/syslog for log messages */
    switch (mode) {
      case Attribute::AccessMode::kRead:
        LOG(INFO) << "module " << name() << ": " << attr_name << ", "
                  << attr_size << " bytes, read" << std::endl;
        break;
      case Attribute::AccessMode::kWrite:
        LOG(INFO) << "module " << name() << ": " << attr_name << ", "
                  << attr_size << " bytes, write" << std::endl;
        break;
      case Attribute::AccessMode::kUpdate:
        LOG(INFO) << "module " << name() << ": " << attr_name << ", "
                  << attr_size << " bytes, update" << std::endl;
        break;
    }
  }

  return pb_errno(0);
}

struct snobj *MetadataTest::AddAttributes(struct snobj *attributes,
                                          Attribute::AccessMode mode) {
  if (snobj_type(attributes) != TYPE_MAP) {
    return snobj_err(EINVAL,
                     "argument must be a map of "
                     "{'attribute name': size, ...}");
  }

  /* a bit hacky, since there is no iterator for maps... */
  for (size_t i = 0; i < attributes->size; i++) {
    int ret;

    const char *attr_name = attributes->map.arr_k[i];
    int attr_size = snobj_int_get((attributes->map.arr_v[i]));

    ret = AddMetadataAttr(attr_name, attr_size, mode);
    if (ret < 0)
      return snobj_err(-ret, "invalid metadata declaration");

    /* check /var/log/syslog for log messages */
    switch (mode) {
      case Attribute::AccessMode::kRead:
        LOG(INFO) << "module " << name() << ": " << attr_name << ", "
                  << attr_size << " bytes, read" << std::endl;
        break;
      case Attribute::AccessMode::kWrite:
        LOG(INFO) << "module " << name() << ": " << attr_name << ", "
                  << attr_size << " bytes, write" << std::endl;
        break;
      case Attribute::AccessMode::kUpdate:
        LOG(INFO) << "module " << name() << ": " << attr_name << ", "
                  << attr_size << " bytes, update" << std::endl;
        break;
    }
  }

  return nullptr;
}

pb_error_t MetadataTest::InitPb(const bess::pb::MetadataTestArg &arg) {
  pb_error_t err;

  err = AddAttributes(arg.read(), Attribute::AccessMode::kRead);
  if (err.err() != 0) {
    return err;
  }

  err = AddAttributes(arg.write(), Attribute::AccessMode::kWrite);
  if (err.err() != 0) {
    return err;
  }

  err = AddAttributes(arg.update(), Attribute::AccessMode::kUpdate);
  if (err.err() != 0) {
    return err;
  }

  return pb_errno(0);
}

struct snobj *MetadataTest::Init(struct snobj *arg) {
  struct snobj *attributes;
  struct snobj *err;

  if ((attributes = snobj_eval(arg, "read"))) {
    err = AddAttributes(attributes, Attribute::AccessMode::kRead);
    if (err) {
      return err;
    }
  }

  if ((attributes = snobj_eval(arg, "write"))) {
    err = AddAttributes(attributes, Attribute::AccessMode::kWrite);
    if (err) {
      return err;
    }
  }

  if ((attributes = snobj_eval(arg, "update"))) {
    err = AddAttributes(attributes, Attribute::AccessMode::kUpdate);
    if (err) {
      return err;
    }
  }

  return nullptr;
}

void MetadataTest::ProcessBatch(struct pkt_batch *batch) {
  /* This module simply passes packets from input gate X down
   * to output gate X (the same gate index) */
  RunChooseModule(get_igate(), batch);
}

ADD_MODULE(MetadataTest, "mt_test", "Dynamic metadata test module")
