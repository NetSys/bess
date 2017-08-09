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

#include <x86intrin.h>

#include <cmath>

#include "set_metadata.h"

#include "../utils/bits.h"
#include "../utils/endian.h"

using bess::metadata::mt_offset_t;

template <bool do_shift = false, bool do_mask = false>
static void CopyFromPacket(bess::PacketBatch *batch, const struct Attr *attr,
                           mt_offset_t mt_off) {
  int cnt = batch->cnt();
  int size = attr->size;
  int shift = attr->shift;
  int pkt_off = attr->offset;

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    uint8_t *head = pkt->head_data<uint8_t *>(pkt_off);
    uint8_t *mt_ptr = _ptr_attr_with_offset<uint8_t>(mt_off, pkt);
    bess::utils::CopySmall(mt_ptr, head, size);
    if (do_shift) {
      if (shift > 0) {
        bess::utils::ShiftBytesRight(mt_ptr, size, shift);
      } else {
        bess::utils::ShiftBytesLeft(mt_ptr, size, -shift);
      }
    }
    if (do_mask) {
      bess::utils::MaskBytes(mt_ptr, attr->mask.bytes, size);
    }
  }
}

static void CopyFromValue(bess::PacketBatch *batch, const struct Attr *attr,
                          mt_offset_t mt_off) {
  int cnt = batch->cnt();
  int size = attr->size;

  const void *val_ptr = &attr->value;

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    void *mt_ptr;

    mt_ptr = _ptr_attr_with_offset<value_t>(mt_off, pkt);
    bess::utils::CopySmall(mt_ptr, val_ptr, size);
  }
}

CommandResponse SetMetadata::AddAttrOne(
    const bess::pb::SetMetadataArg_Attribute &attr) {
  std::string name;
  size_t size = 0;
  int offset = -1;
  int rshift_bytes = attr.rshift_bits() / 8;
  value_t value;
  mask_t mask;
  bool do_mask = false;

  int ret;

  if (!attr.name().length()) {
    return CommandFailure(EINVAL, "'name' field is missing");
  }
  name = attr.name();
  size = attr.size();
  do_mask = attr.mask().length() > 0;

  if (size < 1 || size > kMetadataAttrMaxSize) {
    return CommandFailure(EINVAL, "'size' must be 1-%zu", kMetadataAttrMaxSize);
  }

  if (do_mask && attr.offset() < 0) {
    return CommandFailure(
        EINVAL, "'mask' may only be set when copying metadata from a packet.");
  }

  if (attr.rshift_bits() && attr.offset() < 0) {
    return CommandFailure(
        EINVAL,
        "'rshift_bits' may only be set when copying metadata from a packet.");
  }

  // All metadata values are stored in a reserved area of packet data.
  // Note they are stored in network order. This does not mean that you need
  // to pass endian-swapped values in value_int to the module. Value is just
  // value, and it has nothing to do with endianness (how an integer value is
  // stored in memory). value_bin is a short stream of bytes, which means that
  // its data will never be reordered.
  if (attr.value_case() == bess::pb::SetMetadataArg_Attribute::kValueInt) {
    if (!bess::utils::uint64_to_bin(&value, attr.value_int(), size, true)) {
      return CommandFailure(EINVAL,
                            "'value_int' field has not a "
                            "correct %zu-byte value",
                            size);
    }
  } else if (attr.value_case() ==
             bess::pb::SetMetadataArg_Attribute::kValueBin) {
    if (attr.value_bin().length() != size) {
      return CommandFailure(EINVAL,
                            "'value_bin' field has not a "
                            "correct %zu-byte value",
                            size);
    }
    bess::utils::Copy(&value, attr.value_bin().data(), size);
  } else {
    offset = attr.offset();
    if (offset < 0 || offset + size >= SNBUF_DATA) {
      return CommandFailure(EINVAL, "invalid packet offset");
    }
    if (rshift_bytes * 8 != attr.rshift_bits()) {
      return CommandFailure(EINVAL, "'rshift_bits' must be a multiple of 8");
    }
    if (static_cast<size_t>(std::abs(rshift_bytes)) >= size) {
      return CommandFailure(EINVAL, "'rshift_bits' must be in (-%zu, %zu)",
                            8 * size, 8 * size);
    }
    if (do_mask) {
      if (attr.mask().length() != size) {
        return CommandFailure(EINVAL,
                              "'mask' field has not a "
                              "correct %zu-byte value",
                              size);
      }
      bess::utils::Copy(&mask, attr.mask().data(), size);
    }
  }

  ret = AddMetadataAttr(name, size,
                        bess::metadata::Attribute::AccessMode::kWrite);
  if (ret < 0) {
    return CommandFailure(-ret, "add_metadata_attr() failed");
  }

  attrs_.push_back({.name = name,
                    .value = value,
                    .mask = mask,
                    .offset = offset,
                    .size = size,
                    .do_mask = do_mask,
                    .shift = rshift_bytes});

  return CommandSuccess();
}

CommandResponse SetMetadata::Init(const bess::pb::SetMetadataArg &arg) {
  if (!arg.attrs_size()) {
    return CommandFailure(EINVAL, "'attrs' must be specified");
  }

  for (int i = 0; i < arg.attrs_size(); i++) {
    const auto &attr = arg.attrs(i);
    CommandResponse err;

    err = AddAttrOne(attr);
    if (err.error().code() != 0) {
      return err;
    }
  }

  return CommandSuccess();
}

template <SetMetadata::Mode mode>
inline void SetMetadata::DoProcessBatch(bess::PacketBatch *batch,
                                        const struct Attr *attr,
                                        mt_offset_t mt_offset) {
  if (mode == Mode::FromPacket) {
    bool shift = attr->shift != 0;
    if (shift && attr->do_mask) {
      CopyFromPacket<true, true>(batch, attr, mt_offset);
    } else if (shift && !attr->do_mask) {
      CopyFromPacket<true, false>(batch, attr, mt_offset);
    } else if (!shift && attr->do_mask) {
      CopyFromPacket<false, true>(batch, attr, mt_offset);
    } else if (!shift && !attr->do_mask) {
      CopyFromPacket<false, false>(batch, attr, mt_offset);
    }
  } else {
    CopyFromValue(batch, attr, mt_offset);
  }
}

void SetMetadata::ProcessBatch(bess::PacketBatch *batch) {
  for (size_t i = 0; i < attrs_.size(); i++) {
    const struct Attr *attr = &attrs_[i];
    mt_offset_t mt_offset = attr_offset(i);

    if (!bess::metadata::IsValidOffset(mt_offset)) {
      continue;
    }

    if (attr->offset >= 0) {
      DoProcessBatch<Mode::FromPacket>(batch, attr, mt_offset);
    } else {
      DoProcessBatch<Mode::FromValue>(batch, attr, mt_offset);
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(SetMetadata, "setattr", "Set metadata attributes to packets")
