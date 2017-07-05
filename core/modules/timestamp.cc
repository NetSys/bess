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

#include "timestamp.h"

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/time.h"
#include "../utils/udp.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Udp;

static inline void timestamp_packet(bess::Packet *pkt, size_t offset,
                                    uint64_t time) {
  Timestamp::MarkerType *marker;
  uint64_t *ts;

  const size_t kStampSize = sizeof(*marker) + sizeof(*ts);
  size_t room = pkt->data_len() - offset;

  if (room < kStampSize) {
    void *ret = pkt->append(kStampSize - room);
    if (!ret) {
      // not enough tailroom for timestamp. give up
      return;
    }
  }

  marker = pkt->head_data<Timestamp::MarkerType *>(offset);
  *marker = Timestamp::kMarker;
  ts = reinterpret_cast<uint64_t *>(marker + 1);
  *ts = time;
}

CommandResponse Timestamp::Init(const bess::pb::TimestampArg &arg) {
  if (arg.offset()) {
    offset_ = arg.offset();
  } else {
    offset_ = sizeof(Ethernet) + sizeof(Ipv4) + sizeof(Udp);
  }
  return CommandSuccess();
}

void Timestamp::ProcessBatch(bess::PacketBatch *batch) {
  // We don't use ctx->current_ns here for better accuracy
  uint64_t now_ns = tsc_to_ns(rdtsc());
  size_t offset = offset_;

  for (int i = 0; i < batch->cnt(); i++) {
    timestamp_packet(batch->pkts()[i], offset, now_ns);
  }

  RunNextModule(batch);
}

ADD_MODULE(Timestamp, "timestamp",
           "marks current time to packets (paired with Measure module)")
