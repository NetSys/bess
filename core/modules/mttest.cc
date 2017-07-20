// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "mttest.h"

#include <glog/logging.h>

CommandResponse MetadataTest::AddAttributes(
    const google::protobuf::Map<std::string, int64_t> &attributes,
    Attribute::AccessMode mode) {
  for (const auto &kv : attributes) {
    int ret;

    const char *attr_name = kv.first.c_str();
    int attr_size = kv.second;

    ret = AddMetadataAttr(attr_name, attr_size, mode);
    if (ret < 0)
      return CommandFailure(-ret, "invalid metadata declaration");

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

  return CommandSuccess();
}

CommandResponse MetadataTest::Init(const bess::pb::MetadataTestArg &arg) {
  CommandResponse err;

  err = AddAttributes(arg.read(), Attribute::AccessMode::kRead);
  if (err.error().code() != 0) {
    return err;
  }

  err = AddAttributes(arg.write(), Attribute::AccessMode::kWrite);
  if (err.error().code() != 0) {
    return err;
  }

  err = AddAttributes(arg.update(), Attribute::AccessMode::kUpdate);
  if (err.error().code() != 0) {
    return err;
  }

  return CommandSuccess();
}

void MetadataTest::ProcessBatch(bess::PacketBatch *batch) {
  /* This module simply passes packets from input gate X down
   * to output gate X (the same gate index) */
  RunChooseModule(get_igate(), batch);
}

ADD_MODULE(MetadataTest, "mt_test", "Dynamic metadata test module")
