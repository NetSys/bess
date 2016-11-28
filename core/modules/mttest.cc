#include "mttest.h"

#include <glog/logging.h>

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

pb_error_t MetadataTest::Init(const bess::pb::MetadataTestArg &arg) {
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

void MetadataTest::ProcessBatch(bess::PacketBatch *batch) {
  /* This module simply passes packets from input gate X down
   * to output gate X (the same gate index) */
  RunChooseModule(get_igate(), batch);
}

ADD_MODULE(MetadataTest, "mt_test", "Dynamic metadata test module")
