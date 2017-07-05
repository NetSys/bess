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

#include "rewrite.h"

#include <cstdio>

#include "../utils/copy.h"

const Commands Rewrite::cmds = {
    {"add", "RewriteArg", MODULE_CMD_FUNC(&Rewrite::CommandAdd),
     Command::THREAD_UNSAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&Rewrite::CommandClear),
     Command::THREAD_UNSAFE},
};

CommandResponse Rewrite::Init(const bess::pb::RewriteArg &arg) {
  return CommandAdd(arg);
}

CommandResponse Rewrite::CommandAdd(const bess::pb::RewriteArg &arg) {
  size_t curr = num_templates_;

  if (curr + arg.templates_size() > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "max %zu packet templates can be used %zu %d",
                          bess::PacketBatch::kMaxBurst, curr,
                          arg.templates_size());
  }

  for (int i = 0; i < arg.templates_size(); i++) {
    const auto &templ = arg.templates(i);

    if (templ.length() > kMaxTemplateSize) {
      return CommandFailure(EINVAL, "template is too big");
    }

    memset(templates_[curr + i], 0, kMaxTemplateSize);
    bess::utils::Copy(templates_[curr + i], templ.c_str(), templ.length());
    template_size_[curr + i] = templ.length();
  }

  num_templates_ = curr + arg.templates_size();
  if (num_templates_ == 0) {
    return CommandSuccess();
  }

  for (size_t i = num_templates_; i < kNumSlots; i++) {
    size_t j = i % num_templates_;
    bess::utils::Copy(templates_[i], templates_[j], template_size_[j]);
    template_size_[i] = template_size_[j];
  }

  return CommandSuccess();
}

CommandResponse Rewrite::CommandClear(const bess::pb::EmptyArg &) {
  next_turn_ = 0;
  num_templates_ = 0;
  return CommandSuccess();
}

inline void Rewrite::DoRewriteSingle(bess::PacketBatch *batch) {
  const int cnt = batch->cnt();
  uint16_t size = template_size_[0];
  const void *templ = templates_[0];

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    char *ptr = pkt->buffer<char *>() + SNBUF_HEADROOM;

    pkt->set_data_off(SNBUF_HEADROOM);
    pkt->set_total_len(size);
    pkt->set_data_len(size);

    bess::utils::CopyInlined(ptr, templ, size, true);
  }
}

inline void Rewrite::DoRewrite(bess::PacketBatch *batch) {
  size_t start = next_turn_;
  const size_t cnt = batch->cnt();

  for (size_t i = 0; i < cnt; i++) {
    uint16_t size = template_size_[start + i];
    bess::Packet *pkt = batch->pkts()[i];
    char *ptr = pkt->buffer<char *>() + SNBUF_HEADROOM;

    pkt->set_data_off(SNBUF_HEADROOM);
    pkt->set_total_len(size);
    pkt->set_data_len(size);

    bess::utils::CopyInlined(ptr, templates_[start + i], size, true);
  }

  next_turn_ = start + cnt;
  if (next_turn_ >= bess::PacketBatch::kMaxBurst) {
    next_turn_ -= bess::PacketBatch::kMaxBurst;
  }
}

void Rewrite::ProcessBatch(bess::PacketBatch *batch) {
  if (num_templates_ == 1) {
    DoRewriteSingle(batch);
  } else if (num_templates_ > 1) {
    DoRewrite(batch);
  }

  RunNextModule(batch);
}

ADD_MODULE(Rewrite, "rewrite", "replaces entire packet data")
