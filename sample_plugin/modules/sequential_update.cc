// Copyright (c) 2014-2017, The Regents of the University of California.
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

#include "sequential_update.h"

using bess::utils::be32_t;

const Commands SequentialUpdate::cmds = {
    {"add", "SequentialUpdateArg",
     MODULE_CMD_FUNC(&SequentialUpdate::CommandAdd), Command::THREAD_UNSAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&SequentialUpdate::CommandClear),
     Command::THREAD_UNSAFE},
};

CommandResponse
SequentialUpdate::Init(const sample::supdate::pb::SequentialUpdateArg &arg) {
  return CommandAdd(arg);
}

CommandResponse SequentialUpdate::CommandAdd(
    const sample::supdate::pb::SequentialUpdateArg &arg) {
  size_t curr = num_vars_;
  if (curr + arg.fields_size() > kMaxVariable) {
    return CommandFailure(EINVAL, "max %zu variables can be specified",
                          kMaxVariable);
  }

  for (int i = 0; i < arg.fields_size(); i++) {
    const auto &var = arg.fields(i);

    size_t size;
    size_t offset;
    be32_t mask;
    uint32_t min;
    uint32_t max;

    offset = var.offset();
    size = var.size();
    min = var.min();
    max = var.max();

    switch (size) {
    case 1:
      mask = be32_t(0x00ffffff);
      min = std::min(min, static_cast<uint32_t>(0xff));
      max = std::min(max, static_cast<uint32_t>(0xff));
      break;

    case 2:
      mask = be32_t(0x0000ffff);
      min = std::min(min, static_cast<uint32_t>(0xffff));
      max = std::min(max, static_cast<uint32_t>(0xffff));
      break;

    case 4:
      mask = be32_t(0x00000000);
      min = std::min(min, static_cast<uint32_t>(0xffffffffu));
      max = std::min(max, static_cast<uint32_t>(0xffffffffu));
      break;

    default:
      return CommandFailure(EINVAL, "'size' must be 1, 2, or 4");
    }

    if (offset + size > SNBUF_DATA) {
      return CommandFailure(EINVAL, "too large 'offset'");
    }

    if (min > max) {
      return CommandFailure(EINVAL, "'min' should not be greater than 'max'");
    }

    vars_[curr + i].offset = offset;
    vars_[curr + i].mask = mask;
    vars_[curr + i].min = min;

    // avoid modulo 0
    vars_[curr + i].range = (max - min + 1) ?: 0xffffffff;
    vars_[curr + i].cur = 0;
    vars_[curr + i].bit_shift = (4 - size) * 8;
  }

  num_vars_ = curr + arg.fields_size();
  return CommandSuccess();
}

CommandResponse SequentialUpdate::CommandClear(const bess::pb::EmptyArg &) {
  num_vars_ = 0;
  return CommandSuccess();
}

void SequentialUpdate::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (size_t i = 0; i < num_vars_; i++) {
    const auto var = &vars_[i];

    be32_t mask = var->mask;
    uint32_t min = var->min;
    uint32_t range = var->range;
    uint32_t cur = var->cur;
    size_t offset = var->offset;
    size_t bit_shift = var->bit_shift;

    for (int j = 0; j < cnt; j++) {
      be32_t *p = batch->pkts()[j]->head_data<be32_t *>(offset);
      uint32_t value = min + cur;
      cur = cur + 1;
      if (cur >= range) {
        cur = 0;
      }

      *p = (*p & mask) | (be32_t(value) << bit_shift);
    }

    var->cur = cur;
  }

  RunNextModule(batch);
}

ADD_MODULE(SequentialUpdate, "supdate",
           "updates packet data sequentially in a range")
