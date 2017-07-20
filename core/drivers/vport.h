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

#ifndef BESS_DRIVERS_VPORT_H_
#define BESS_DRIVERS_VPORT_H_

#include "../kmod/sn_common.h"
#include "../port.h"

class VPort final : public Port {
 public:
  VPort() : fd_(), bar_(), map_(), netns_fd_(), container_pid_() {}
  void InitDriver() override;

  CommandResponse Init(const bess::pb::VPortArg &arg);
  void DeInit() override;

  int RecvPackets(queue_t qid, bess::Packet **pkts, int max_cnt) override;
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

 private:
  struct queue {
    union {
      struct sn_rxq_registers *rx_regs;
    };

    struct llring *drv_to_sn;
    struct llring *sn_to_drv;
  };

  void FreeBar();
  void *AllocBar(struct tx_queue_opts *txq_opts,
                 struct rx_queue_opts *rxq_opts);
  int SetIPAddrSingle(const std::string &ip_addr);
  CommandResponse SetIPAddr(const bess::pb::VPortArg &arg);

  int fd_;

  char ifname_[IFNAMSIZ]; /* could be different from Name() */
  void *bar_;

  struct queue inc_qs_[MAX_QUEUES_PER_DIR];
  struct queue out_qs_[MAX_QUEUES_PER_DIR];

  struct sn_ioc_queue_mapping map_;

  int netns_fd_;
  int container_pid_;
};

#endif  // BESS_DRIVERS_VPORT_H_
