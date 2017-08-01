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

#include <cmath>
#include <x86intrin.h>

#include "set_metadata.h"

#include "../utils/endian.h"
#include "../utils/simd.h"

static void CopyFromPacket(bess::PacketBatch *batch, const struct Attr *attr,
                           bess::metadata::mt_offset_t mt_off) {
  int cnt = batch->cnt();
  int size = attr->size;
  int shift = attr->shift;
  int pkt_off = attr->offset;
  int mt_adj = 0;
  if (shift < 0) {
    pkt_off += std::abs(shift);
  } else if (shift > 0) {
    mt_adj += shift;
  }
  size -= std::abs(shift);

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    uint8_t *head = pkt->head_data<uint8_t *>(pkt_off);
    uint8_t *mt_ptr = _ptr_attr_with_offset<uint8_t>(mt_off + mt_adj, pkt);
    bess::utils::CopySmall(mt_ptr, head, size);
  }
}

static void CopyFromPacketWithMask(bess::PacketBatch *batch,
                                   const struct Attr *attr,
                                   bess::metadata::mt_offset_t mt_off) {
  int cnt = batch->cnt();
  int size = attr->size;
  mask_t mask = attr->mask;
  int shift = attr->shift;
  int pkt_off = attr->offset;
  int mt_adj = 0;
  if (shift < 0) {
    pkt_off += std::abs(shift);
  } else if (shift > 0) {
    mt_adj += shift;
  }

  // Copy data
  size -= std::abs(shift);
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    uint8_t *head = pkt->head_data<uint8_t *>(pkt_off);
    uint8_t *mt_ptr = _ptr_attr_with_offset<uint8_t>(mt_off + mt_adj, pkt);
    bess::utils::CopySmall(mt_ptr, head, size);
  }
  size += std::abs(shift);

  // Apply mask
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    int len = size;

    uint64_t *mt64 = _ptr_attr_with_offset<uint64_t>(mt_off + mt_adj, pkt);
    uint64_t *mask64 = reinterpret_cast<uint64_t *>(mask.bytes);
    __m128i *mt128 = reinterpret_cast<__m128i *>(mt64);
    __m128i a = gather_m128i(mt64, mt64 + 1);
    __m128i b = gather_m128i(mask64, mask64 + 1);
    *mt128 = _mm_and_si128(a, b);

    len -= sizeof(__m128i);
    mt128++;
    mt64 += 2;
    mask64 += 2;

    while (len >= int{sizeof(__m128i)}) {
      a = gather_m128i(mt64, mt64 + 1);
      b = gather_m128i(mask64, mask64 + 1);
      *mt128 = _mm_and_si128(a, b);
      len -= sizeof(__m128i);
      mt128++;
      mt64 += 2;
      mask64 += 2;
    }
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
    bess::utils::CopySmall(mt_ptr, val_ptr, size);
  }
}

CommandResponse SetMetadata::AddAttrOne(
    const bess::pb::SetMetadataArg_Attribute &attr) {
  std::string name;
  size_t size = 0;
  int offset = -1;
  value_t value = value_t();
  mask_t mask = mask_t();
  bool do_mask = false;

  int ret;

  if (!attr.name().length()) {
    return CommandFailure(EINVAL, "'name' field is missing");
  }
  name = attr.name();
  size = attr.size();
  do_mask = attr.mask().length() > 0;

  if (size < 1 || size > bess::metadata::kMetadataAttrMaxSize) {
    return CommandFailure(EINVAL, "'size' must be 1-%zu",
                          bess::metadata::kMetadataAttrMaxSize);
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
    if (do_mask) {
      memset(mask.bytes, 0xFF, size);
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
  if (ret < 0)
    return CommandFailure(-ret, "add_metadata_attr() failed");

  attrs_.emplace_back();
  attrs_.back().name = name;
  attrs_.back().size = size;
  attrs_.back().offset = offset;
  attrs_.back().value = value;
  attrs_.back().do_mask = do_mask;
  attrs_.back().mask = mask;
  attrs_.back().shift = attr.shift();

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

void SetMetadata::ProcessBatch(bess::PacketBatch *batch) {
  for (size_t i = 0; i < attrs_.size(); i++) {
    const struct Attr *attr = &attrs_[i];

    bess::metadata::mt_offset_t mt_offset = attr_offset(i);

    if (!bess::metadata::IsValidOffset(mt_offset)) {
      continue;
    }

    /* copy data from the packet */
    if (attr->offset >= 0) {
      if (attr->do_mask) {
        CopyFromPacketWithMask(batch, attr, mt_offset);
      } else {
        CopyFromPacket(batch, attr, mt_offset);
      }
    } else {
      CopyFromValue(batch, attr, mt_offset);
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(SetMetadata, "setattr", "Set metadata attributes to packets")
