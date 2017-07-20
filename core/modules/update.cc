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

#include "update.h"

#include "../utils/endian.h"

const Commands Update::cmds = {
    {"add", "UpdateArg", MODULE_CMD_FUNC(&Update::CommandAdd),
     Command::THREAD_UNSAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&Update::CommandClear),
     Command::THREAD_UNSAFE},
};

CommandResponse Update::Init(const bess::pb::UpdateArg &arg) {
  return CommandAdd(arg);
}

void Update::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (size_t i = 0; i < num_fields_; i++) {
    const auto field = &fields_[i];

    be64_t mask = field->mask;
    be64_t value = field->value;
    int16_t offset = field->offset;  // could be < 0

    for (int j = 0; j < cnt; j++) {
      bess::Packet *snb = batch->pkts()[j];
      char *head = snb->head_data<char *>();

      be64_t *p = reinterpret_cast<be64_t *>(head + offset);
      *p = (*p & mask) | value;
    }
  }

  RunNextModule(batch);
}

CommandResponse Update::CommandAdd(const bess::pb::UpdateArg &arg) {
  size_t curr = num_fields_;

  if (curr + arg.fields_size() > kMaxFields) {
    return CommandFailure(EINVAL, "max %zu variables can be specified",
                          kMaxFields);
  }

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &field = arg.fields(i);

    size_t size = field.size();
    if (size < 1 || size > 8) {
      return CommandFailure(EINVAL, "'size' must be 1-8");
    }

    if (field.offset() + size > SNBUF_DATA) {
      return CommandFailure(EINVAL, "too large 'offset'");
    }

    be64_t value(field.value() << ((8 - size) * 8));
    be64_t mask((1ull << ((8 - size) * 8)) - 1);

    if ((value & mask).value() != 0) {
      LOG(INFO) << value << ' ' << mask;
      return CommandFailure(
          EINVAL, "'value' field has not a correct %zu-byte value", size);
    }

    fields_[curr + i].offset = field.offset();
    fields_[curr + i].mask = mask;
    fields_[curr + i].value = value;
  }

  num_fields_ = curr + arg.fields_size();
  return CommandSuccess();
}

CommandResponse Update::CommandClear(const bess::pb::EmptyArg &) {
  num_fields_ = 0;
  return CommandSuccess();
}

ADD_MODULE(Update, "update", "updates packet data with specified values")
