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

#include "dump.h"

#include <cmath>
#include <iostream>

#include <rte_hexdump.h>

#define NS_PER_SEC 1000000000ull

static const uint64_t DEFAULT_INTERVAL_NS = 1 * NS_PER_SEC; /* 1 sec */

const Commands Dump::cmds = {
    {"set_interval", "DumpArg", MODULE_CMD_FUNC(&Dump::CommandSetInterval),
     Command::THREAD_UNSAFE},
};

CommandResponse Dump::Init(const bess::pb::DumpArg &arg) {
  min_interval_ns_ = DEFAULT_INTERVAL_NS;
  next_ns_ = ctx.current_tsc();
  return CommandSetInterval(arg);
}

void Dump::ProcessBatch(bess::PacketBatch *batch) {
  if (unlikely(ctx.current_ns() >= next_ns_)) {
    bess::Packet *pkt = batch->pkts()[0];

    printf("----------------------------------------\n");
    printf("%s: packet dump\n", name().c_str());
    std::cout << pkt->Dump();
    rte_hexdump(stdout, "Metadata buffer", pkt->metadata<const char *>(),
                SNBUF_METADATA);
    next_ns_ = ctx.current_ns() + min_interval_ns_;
  }

  RunChooseModule(get_igate(), batch);
}

CommandResponse Dump::CommandSetInterval(const bess::pb::DumpArg &arg) {
  double sec = arg.interval();

  if (std::isnan(sec) || sec <= 0.0) {
    return CommandFailure(EINVAL, "invalid interval");
  }

  min_interval_ns_ = static_cast<uint64_t>(sec * NS_PER_SEC);
  return CommandSuccess();
}

ADD_MODULE(Dump, "dump", "Dump packet data and metadata attributes")
