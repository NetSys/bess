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

#include "hash_lb.h"

#include <rte_hash_crc.h>

const enum LbMode DEFAULT_MODE = LB_L4;

static inline uint32_t hash_64(uint64_t val, uint32_t init_val) {
#if __SSE4_2__ && __x86_64
  return crc32c_sse42_u64(val, init_val);
#else
  return crc32c_2words(val, init_val);
#endif
}

/* Returns a value in [0, range) as a function of an opaque number.
 * Also see utils/random.h */
static inline uint16_t hash_range(uint32_t hashval, uint16_t range) {
#if 1
  union {
    uint64_t i;
    double d;
  } tmp;

  /* the resulting number is 1.(b0)(b1)..(b31)00000..00 */
  tmp.i = 0x3ff0000000000000ull | (static_cast<uint64_t>(hashval) << 20);

  return (tmp.d - 1.0) * range;
#else
  /* This IDIV instruction is significantly slower */
  return hashval % range;
#endif
}

static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

const Commands HashLB::cmds = {
    {"set_mode", "HashLBCommandSetModeArg",
     MODULE_CMD_FUNC(&HashLB::CommandSetMode), Command::THREAD_UNSAFE},
    {"set_gates", "HashLBCommandSetGatesArg",
     MODULE_CMD_FUNC(&HashLB::CommandSetGates), Command::THREAD_UNSAFE}};

CommandResponse HashLB::CommandSetMode(
    const bess::pb::HashLBCommandSetModeArg &arg) {
  if (arg.mode() == "l2") {
    mode_ = LB_L2;
  } else if (arg.mode() == "l3") {
    mode_ = LB_L3;
  } else if (arg.mode() == "l4") {
    mode_ = LB_L4;
  } else {
    return CommandFailure(EINVAL, "available LB modes: l2, l3, l4");
  }

  return CommandSuccess();
}

CommandResponse HashLB::CommandSetGates(
    const bess::pb::HashLBCommandSetGatesArg &arg) {
  if (arg.gates_size() > MAX_HLB_GATES) {
    return CommandFailure(EINVAL, "no more than %d gates", MAX_HLB_GATES);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    gates_[i] = arg.gates(i);
    if (!is_valid_gate(gates_[i])) {
      return CommandFailure(EINVAL, "invalid gate %d", gates_[i]);
    }
  }

  num_gates_ = arg.gates_size();
  return CommandSuccess();
}

CommandResponse HashLB::Init(const bess::pb::HashLBArg &arg) {
  mode_ = DEFAULT_MODE;

  if (arg.gates_size() > MAX_HLB_GATES) {
    return CommandFailure(EINVAL, "no more than %d gates", MAX_HLB_GATES);
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    gates_[i] = arg.gates(i);
    if (!is_valid_gate(gates_[i])) {
      return CommandFailure(EINVAL, "invalid gate %d", gates_[i]);
    }
  }

  num_gates_ = arg.gates_size();

  if (arg.mode() == "l2") {
    mode_ = LB_L2;
  } else if (arg.mode() == "l3") {
    mode_ = LB_L3;
  } else if (arg.mode() == "l4") {
    mode_ = LB_L4;
  } else {
    return CommandFailure(EINVAL, "available LB modes: l2, l3, l4");
  }

  return CommandSuccess();
}

void HashLB::LbL2(bess::PacketBatch *batch, gate_idx_t *out_gates) {
  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *snb = batch->pkts()[i];
    char *head = snb->head_data<char *>();

    uint64_t v0 = *(reinterpret_cast<uint64_t *>(head));
    uint32_t v1 = *(reinterpret_cast<uint32_t *>(head + 8));

    uint32_t hash_val = hash_64(v0, v1);

    out_gates[i] = gates_[hash_range(hash_val, num_gates_)];
  }
}

void HashLB::LbL3(bess::PacketBatch *batch, gate_idx_t *out_gates) {
  /* assumes untagged packets */
  const int ip_offset = 14;

  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *snb = batch->pkts()[i];
    char *head = snb->head_data<char *>();

    uint32_t hash_val;
    uint64_t v = *(reinterpret_cast<uint64_t *>(head + ip_offset + 12));

    hash_val = hash_64(v, 0);

    out_gates[i] = gates_[hash_range(hash_val, num_gates_)];
  }
}

void HashLB::LbL4(bess::PacketBatch *batch, gate_idx_t *out_gates) {
  /* assumes untagged packets without IP options */
  const int ip_offset = 14;
  const int l4_offset = ip_offset + 20;

  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *snb = batch->pkts()[i];
    char *head = snb->head_data<char *>();

    uint32_t hash_val;
    uint64_t v0 = *(reinterpret_cast<uint64_t *>(head + ip_offset + 12));
    uint32_t v1 = *(reinterpret_cast<uint64_t *>(head + l4_offset)); /* ports */

    v1 ^= *(reinterpret_cast<uint32_t *>(head + ip_offset + 9)); /* ip_proto */

    hash_val = hash_64(v0, v1);

    out_gates[i] = gates_[hash_range(hash_val, num_gates_)];
  }
}

void HashLB::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];

  switch (mode_) {
    case LB_L2:
      LbL2(batch, out_gates);
      break;

    case LB_L3:
      LbL3(batch, out_gates);
      break;

    case LB_L4:
      LbL4(batch, out_gates);
      break;

    default:
      DCHECK(0);
  }

  RunSplit(out_gates, batch);
}

ADD_MODULE(HashLB, "hash_lb",
           "splits packets on a flow basis with L2/L3/L4 header fields")
