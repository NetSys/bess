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

#ifndef BESS_DRIVERS_ZERO_COPY_VPORT_
#define BESS_DRIVERS_ZERO_COPY_VPORT_
#include <gtest/gtest.h>

#include "../kmod/llring.h"
#include "../message.h"
#include "../port.h"

#define SLOTS_PER_LLRING 1024

/* This watermark is to detect congestion and cache bouncing due to
 * head-eating-tail (needs at least 8 slots less then the total ring slots).
 * Not sure how to tune this... */
#define SLOTS_WATERMARK ((SLOTS_PER_LLRING >> 3) * 7) /* 87.5% */

/* Disable (0) single producer/consumer mode for now.
 * This is slower, but just to be on the safe side. :) */
#define SINGLE_P 0
#define SINGLE_C 0

#define PORT_NAME_LEN 128

#define VPORT_DIR_PREFIX "sn_vports"

struct vport_inc_regs {
  uint64_t dropped;
} __cacheline_aligned;

struct vport_out_regs {
  uint32_t irq_enabled;
} __cacheline_aligned;

/* This is equivalent to the old bar */
struct vport_bar {
  char name[PORT_NAME_LEN];

  /* The term RX/TX could be very confusing for a virtual switch.
   * Instead, we use the "incoming/outgoing" convention:
   * - incoming: outside -> BESS
   * - outgoing: BESS -> outside */
  int num_inc_q;
  int num_out_q;

  struct vport_inc_regs *inc_regs[MAX_QUEUES_PER_DIR];
  struct llring *inc_qs[MAX_QUEUES_PER_DIR];

  struct vport_out_regs *out_regs[MAX_QUEUES_PER_DIR];
  struct llring *out_qs[MAX_QUEUES_PER_DIR];
};

class ZeroCopyVPort final : public Port {
 public:
  CommandResponse Init(const bess::pb::EmptyArg &arg);

  void DeInit() override;

  int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

 private:
  friend class ZeroCopyVPortTest;
  FRIEND_TEST(ZeroCopyVPortTest, Recv);

  struct vport_bar *bar_ = {};

  struct vport_inc_regs *inc_regs_[MAX_QUEUES_PER_DIR] = {};
  struct llring *inc_qs_[MAX_QUEUES_PER_DIR] = {};

  struct vport_out_regs *out_regs_[MAX_QUEUES_PER_DIR] = {};
  struct llring *out_qs_[MAX_QUEUES_PER_DIR] = {};

  int out_irq_fd_[MAX_QUEUES_PER_DIR] = {};
};

#endif  // BESS_DRIVERS_ZERO_COPY_VPORT_
