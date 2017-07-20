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

#ifndef BESS_PKTBATCH_H_
#define BESS_PKTBATCH_H_

#include "utils/copy.h"

namespace bess {

class Packet;

class PacketBatch {
 public:
  int cnt() const { return cnt_; }
  void set_cnt(int cnt) { cnt_ = cnt; }
  void incr_cnt(int n = 1) { cnt_ += n; }

  Packet *const *pkts() const { return pkts_; }
  Packet **pkts() { return pkts_; }

  void clear() { cnt_ = 0; }

  // WARNING: this function has no bounds checks and so it's possible to
  // overrun the buffer by calling this. We are not adding bounds check because
  // we want maximum GOFAST.
  void add(Packet *pkt) { pkts_[cnt_++] = pkt; }

  bool empty() { return (cnt_ == 0); }

  bool full() { return (cnt_ == kMaxBurst); }

  void Copy(const PacketBatch *src) {
    cnt_ = src->cnt_;
    bess::utils::CopyInlined(pkts_, src->pkts_, cnt_ * sizeof(Packet *));
  }

  static const size_t kMaxBurst = 32;

 private:
  int cnt_;
  Packet *pkts_[kMaxBurst];
};

static_assert(std::is_pod<PacketBatch>::value, "PacketBatch is not a POD Type");

}  // namespace bess

#endif  // BESS_PKTBATCH_H_
