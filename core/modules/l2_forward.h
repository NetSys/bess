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

#ifndef BESS_MODULES_L2FORWARD_H_
#define BESS_MODULES_L2FORWARD_H_

#include "../module.h"
#include "../pb/module_msg.pb.h"

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

struct alignas(32) l2_entry {
  union {
    struct {
      uint64_t addr : 48;
      uint64_t gate : 15;
      uint64_t occupied : 1;
    };
    uint64_t entry;
  };
};

struct l2_table {
  struct l2_entry *table;
  uint64_t size;
  uint64_t size_power;
  uint64_t bucket;
  uint64_t count;
};

class L2Forward final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  L2Forward() : Module(), l2_table_(), default_gate_() {
    max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const bess::pb::L2ForwardArg &arg);

  void DeInit() override;

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::L2ForwardCommandAddArg &arg);
  CommandResponse CommandDelete(const bess::pb::L2ForwardCommandDeleteArg &arg);
  CommandResponse CommandSetDefaultGate(
      const bess::pb::L2ForwardCommandSetDefaultGateArg &arg);
  CommandResponse CommandLookup(const bess::pb::L2ForwardCommandLookupArg &arg);
  CommandResponse CommandPopulate(
      const bess::pb::L2ForwardCommandPopulateArg &arg);

 private:
  struct l2_table l2_table_;
  gate_idx_t default_gate_;
};

#endif  // BESS_MODULES_L2FORWARD_H_
