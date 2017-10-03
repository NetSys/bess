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

#include "split.h"

#include "../utils/endian.h"

// XXX: this is repeated in many modules. get rid of them when converting .h to
// .hh, etc... it's in defined in some old header
static inline bool is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

CommandResponse Split::Init(const bess::pb::SplitArg &arg) {
  size_ = arg.size();
  if (size_ < 1 || size_ > sizeof(uint64_t)) {
    return CommandFailure(EINVAL, "'size' must be 1-%zu", sizeof(uint64_t));
  }

  mask_ = (size_ == 8) ? 0xffffffffffffffffull : (1ull << (size_ * 8)) - 1;

  // We read a be64_t value regardless of the actual size,
  // hence the read value needs bit shift to the right.
  shift_ = 64 - (size_ * 8);

  if (arg.type_case() == bess::pb::SplitArg::kAttribute) {
    attr_id_ = AddMetadataAttr(arg.attribute().c_str(), size_,
                               bess::metadata::Attribute::AccessMode::kRead);
    if (attr_id_ < 0) {
      return CommandFailure(-attr_id_, "add_metadata_attr() failed");
    }
  } else {
    attr_id_ = -1;
    offset_ = arg.offset();
    if (offset_ > 1024) {
      return CommandFailure(EINVAL, "invalid 'offset'");
    }
  }
  return CommandSuccess();
}

void Split::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::be64_t;

  gate_idx_t ogate[bess::PacketBatch::kMaxBurst];
  int cnt = batch->cnt();

  if (attr_id_ >= 0) {
    bess::metadata::mt_offset_t offset = attr_offset(attr_id_);
    for (int i = 0; i < cnt; i++) {
      const bess::Packet *pkt = batch->pkts()[i];
      uint64_t val = get_attr_with_offset<be64_t>(offset, pkt).value();
      val = (val >> shift_) & mask_;
      ogate[i] = is_valid_gate(val) ? val : DROP_GATE;
    }
  } else {
    for (int i = 0; i < cnt; i++) {
      const bess::Packet *pkt = batch->pkts()[i];
      uint64_t val = (pkt->head_data<be64_t *>(offset_))->value();
      val = (val >> shift_) & mask_;
      ogate[i] = is_valid_gate(val) ? val : DROP_GATE;
    }
  }

  RunSplit(ogate, batch);
}

ADD_MODULE(Split, "split",
           "split packets depending on packet data or metadata attributes")
