/* x86_64 BPF JIT code was adopted from FreeBSD 10 - Sangjin */

/*-
 * Copyright (C) 2002-2003 NetGroup, Politecnico di Torino (Italy)
 * Copyright (C) 2005-2009 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/mman.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../utils/bpf.h"
#include "bpf.h"

/* -------------------------------------------------------------------------
 * Module code begins from here
 * ------------------------------------------------------------------------- */

/* Note: bpf_filter will return SNAPLEN if matched, and 0 if unmatched. */
/* Note: unmatched packets are sent to gate 0 */
#define SNAPLEN 0xffff

const Commands BPF::cmds = {
    {"add", "BPFArg", MODULE_CMD_FUNC(&BPF::CommandAdd),
     Command::THREAD_UNSAFE},
    {"delete", "BPFArg", MODULE_CMD_FUNC(&BPF::CommandDelete),
     Command::THREAD_UNSAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&BPF::CommandClear),
     Command::THREAD_UNSAFE},
    {"get_initial_arg", "EmptyArg", MODULE_CMD_FUNC(&BPF::GetInitialArg),
     Command::THREAD_SAFE}};

CommandResponse BPF::Init(const bess::pb::BPFArg &arg) {
  return CommandAdd(arg);
}

void BPF::DeInit() {
  for (auto &filter : filters_) {
#ifdef __x86_64
    munmap(reinterpret_cast<void *>(filter.func), filter.mmap_size);
#else
    pcap_freecode(&filter.il_code);
#endif
  }

  filters_.clear();
}

CommandResponse BPF::GetInitialArg(const bess::pb::EmptyArg &) {
  bess::pb::BPFArg r;
  for (auto f : filters_) {
    auto *f_pb = r.add_filters();
    f_pb->set_priority(f.priority);
    f_pb->set_filter(f.exp);
    f_pb->set_gate(f.gate);
  }
  return CommandSuccess(r);
}

CommandResponse BPF::CommandAdd(const bess::pb::BPFArg &arg) {
  for (const auto &f : arg.filters()) {
    if (f.gate() < 0 || f.gate() >= MAX_GATES) {
      return CommandFailure(EINVAL, "Invalid gate");
    }

    bess::utils::Filter filter;
    filter.priority = f.priority();
    filter.gate = f.gate();
    filter.exp = f.filter();

    struct bpf_program il;
    if (pcap_compile_nopcap(SNAPLEN, DLT_EN10MB,  // Ethernet
                            &il, filter.exp.c_str(),
                            1,  // optimize (IL only)
                            PCAP_NETMASK_UNKNOWN) == -1) {
      return CommandFailure(EINVAL, "BPF compilation error");
    }

#ifdef __x86_64
    filter.func =
        bess::utils::bpf_jit_compile(il.bf_insns, il.bf_len, &filter.mmap_size);
    pcap_freecode(&il);
    if (!filter.func) {
      return CommandFailure(ENOMEM, "BPF JIT compilation error");
    }
#else
    filter.il_code = il;
#endif

    filters_.push_back(filter);
  }

  std::sort(filters_.begin(), filters_.end(),
            [](const bess::utils::Filter &a, const bess::utils::Filter &b) {
              // descending order of priority number
              return b.priority < a.priority;
            });

  return CommandSuccess();
}

CommandResponse BPF::CommandDelete(const bess::pb::BPFArg &arg) {
  for (const auto &f : arg.filters()) {
    if (f.gate() < 0 || f.gate() >= MAX_GATES) {
      return CommandFailure(EINVAL, "Invalid gate");
    }

    for (auto i = filters_.begin(); i != filters_.end(); ++i) {
      if (f.priority() == i->priority && f.gate() == i->gate &&
          f.filter() == i->exp) {
        filters_.erase(i);
        break;
      }
    }
  }

  return CommandSuccess();
}

CommandResponse BPF::CommandClear(const bess::pb::EmptyArg &) {
  DeInit();
  return CommandSuccess();
}

inline bool BPF::Match(const bess::utils::Filter &filter, u_char *pkt,
                       u_int wirelen, u_int buflen) {
#ifdef __x86_64
  int ret = filter.func(pkt, wirelen, buflen);
#else
  int ret = bpf_filter(filter.il_code.bf_insns, pkt, wirelen, buflen);
#endif

  return ret != 0;
}

void BPF::ProcessBatch1Filter(Context *ctx, bess::PacketBatch *batch) {
  const bess::utils::Filter &filter = filters_[0];

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    if (Match(filter, pkt->head_data<u_char *>(), pkt->total_len(),
              pkt->head_len())) {
      EmitPacket(ctx, pkt, filter.gate);
    } else {
      EmitPacket(ctx, pkt);
    }
  }
}

void BPF::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  int n_filters = filters_.size();

  if (n_filters == 0) {
    RunNextModule(ctx, batch);
    return;
  } else if (n_filters == 1) {
    ProcessBatch1Filter(ctx, batch);
    return;
  }

  // slow version for general cases
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    gate_idx_t gate = 0;  // default gate for unmatched pkts
    bess::Packet *pkt = batch->pkts()[i];

    // high priority filters are checked first
    for (const bess::utils::Filter &filter : filters_) {
      if (Match(filter, pkt->head_data<uint8_t *>(), pkt->total_len(),
                pkt->head_len())) {
        gate = filter.gate;
        break;
      }
    }
    EmitPacket(ctx, pkt, gate);
  }
}

ADD_MODULE(BPF, "bpf", "classifies packets with pcap-filter(7) syntax")
