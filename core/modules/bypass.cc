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

#include "bypass.h"

CommandResponse Bypass::Init(const bess::pb::BypassArg &arg) {
  cycles_per_batch_ = arg.cycles_per_batch();
  cycles_per_packet_ = arg.cycles_per_packet();
  cycles_per_byte_ = arg.cycles_per_byte();

  return CommandSuccess();
}

void Bypass::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  uint64_t start_tsc = rdtsc();
  uint64_t cycles = cycles_per_batch_ + cycles_per_packet_ * batch->cnt();

  if (cycles_per_byte_) {
    uint64_t total_bytes = 0;
    int cnt = batch->cnt();
    for (int i = 0; i < cnt; i++) {
      total_bytes = batch->pkts()[i]->total_len();
    }
    cycles += cycles_per_byte_ * total_bytes;
  }

  if (cycles) {
    uint64_t target_tsc = start_tsc + cycles;
    // burn cycles until it comsumes target cycles
    while (rdtsc() < target_tsc) {
      _mm_pause();
    }
  }
  RunChooseModule(ctx, ctx->current_igate, batch);
}

ADD_MODULE(Bypass, "bypass", "bypasses packets without any processing")
