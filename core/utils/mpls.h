// Copyright (c) 2017, Cloudigo.
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

#ifndef BESS_UTILS_MPLS_H_
#define BESS_UTILS_MPLS_H_

#include "rte_byteorder.h"

namespace bess {
namespace utils {

// MPLS header definition, Reference: RFC 5462, RFC 3032
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                Label                  | TC  |S|       TTL     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//	Label:  Label Value, 20 bits
//	TC:     Traffic Class field, 3 bits
//	S:      Bottom of Stack, 1 bit
//	TTL:    Time to Live, 8 bits

struct Mpls {
  static const uint32_t kMplsLabelMask = 0xFFFFF000;
  static const uint32_t kMplsLabelShift = 12;
  static const uint32_t kMplsTcMask = 0x00000E00;
  static const uint32_t kMplsTcShift = 9;
  static const uint32_t kMplsBosMask = 0x00000100;
  static const uint32_t kMplsBosShift = 8;
  static const uint32_t kMplsTtlMask = 0x000000FF;
  static const uint32_t kMplsTtlShift = 0;

  void SetEntry(uint32_t label, uint8_t ttl, uint8_t tc, bool bos) {
    tag = be32_t((label << kMplsLabelShift) | (tc << kMplsTcShift) |
                 (bos ? (1 << kMplsBosShift) : 0) | (ttl << kMplsTtlShift));
  }

  uint32_t Label() {
    return (tag.value() & kMplsLabelMask) >> kMplsLabelShift;
  }

  uint8_t Ttl() {
    return (tag.value() & kMplsTtlMask) >> kMplsTtlShift;
  }

  uint8_t Tc() {
    return (tag.value() & kMplsTcMask) >> kMplsTcShift;
  }

  bool isBottomOfStack() {
    return (tag.value() & kMplsBosMask) >> kMplsBosShift;
  }

  be32_t tag;
};

static_assert(sizeof(Mpls) == 4, "struct Mpls size is incorrect");

}  // namespace utils
}  // namespace bess

#endif /* BESS_UTILS_MPLS_H_ */
