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

#include "endian.h"

namespace bess {
namespace utils {

bool uint64_to_bin(void *ptr, uint64_t val, size_t size, bool big_endian) {
  uint8_t *const ptr8 = static_cast<uint8_t *>(ptr);

  if (big_endian) {
    for (uint8_t *p = ptr8 + size - 1; p >= ptr8; p--) {
      *p = val & 0xff;
      val >>= 8;
    }
  } else {
    for (uint8_t *p = ptr8; p < ptr8 + size; p++) {
      *p = val & 0xff;
      val >>= 8;
    }
  }

  if (val) {
    return false;  // the value is too large for the size
  } else {
    return true;
  }
}

bool bin_to_uint64(uint64_t *pval, const void *ptr, size_t size,
                   bool big_endian) {
  const uint8_t *const ptr8 = static_cast<const uint8_t *>(ptr);

  if (size > 8 || size < 1) {
    return false;  // size must be 1-8
  }

  *pval = 0;

  if (big_endian) {
    for (const uint8_t *p = ptr8; p < ptr8 + size; p++) {
      *pval = (*pval << 8) | *p;
    }
  } else {
    for (const uint8_t *p = ptr8 + size - 1; p >= ptr8; p--) {
      *pval = (*pval << 8) | *p;
    }
  }

  return true;
}

}  // namespace utils
}  // namespace bess
