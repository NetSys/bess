// Copyright (c) 2017, Vivian Fang.
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

#include "random_drop.h"
#include <string>
#include <time.h>

CommandResponse RandomDrop::Init(const bess::pb::RandomDropArg &arg) {
  double drop_rate = arg.drop_rate();
  if (drop_rate < 0 || drop_rate > 1) {
    return CommandFailure(EINVAL, "drop rate needs to be between [0, 1]");
  }
  threshold_ = drop_rate * kRange;
  return CommandSuccess();
}

void RandomDrop::ProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch out_batch;
  bess::PacketBatch free_batch;
  out_batch.clear();
  free_batch.clear();

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    if (rng_.GetRange(kRange) > threshold_) {
      out_batch.add(pkt);
    } else {
      free_batch.add(pkt);
    }
  }

  bess::Packet::Free(&free_batch);
  RunNextModule(&out_batch);
}

ADD_MODULE(RandomDrop, "random_drop", "randomly drops packets")
