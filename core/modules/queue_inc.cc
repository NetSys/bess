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

#include "queue_inc.h"

#include "../port.h"
#include "../utils/format.h"

const Commands QueueInc::cmds = {{"set_burst", "QueueIncCommandSetBurstArg",
                                  MODULE_CMD_FUNC(&QueueInc::CommandSetBurst),
                                  Command::THREAD_SAFE}};

CommandResponse QueueInc::Init(const bess::pb::QueueIncArg &arg) {
  const char *port_name;
  task_id_t tid;
  CommandResponse err;
  burst_ = bess::PacketBatch::kMaxBurst;
  if (!arg.port().length()) {
    return CommandFailure(EINVAL, "Field 'port' must be specified");
  }
  port_name = arg.port().c_str();
  qid_ = arg.qid();

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return CommandFailure(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;
  burst_ = bess::PacketBatch::kMaxBurst;

  if (arg.prefetch()) {
    prefetch_ = 1;
  }
  node_constraints_ = port_->GetNodePlacementConstraint();
  tid = RegisterTask((void *)(uintptr_t)qid_);
  if (tid == INVALID_TASK_ID)
    return CommandFailure(ENOMEM, "Task creation failed");

  int ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                                 PACKET_DIR_INC, &qid_, 1);
  if (ret < 0) {
    return CommandFailure(-ret);
  }

  return CommandSuccess();
}

void QueueInc::DeInit() {
  if (port_) {
    port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_INC,
                         &qid_, 1);
  }
}

std::string QueueInc::GetDesc() const {
  return bess::utils::Format("%s:%hhu/%s", port_->name().c_str(), qid_,
                             port_->port_builder()->class_name().c_str());
}

struct task_result QueueInc::RunTask(void *arg) {
  Port *p = port_;

  const queue_t qid = (queue_t)(uintptr_t)arg;

  bess::PacketBatch batch;

  uint64_t received_bytes = 0;

  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  batch.set_cnt(p->RecvPackets(qid, batch.pkts(), burst));
  uint32_t cnt = batch.cnt();

  if (cnt == 0) {
    return {.block = true, .packets = 0, .bits = 0};
  }

  // NOTE: we cannot skip this step since it might be used by scheduler.
  if (prefetch_) {
    for (uint32_t i = 0; i < cnt; i++) {
      received_bytes += batch.pkts()[i]->total_len();
      rte_prefetch0(batch.pkts()[i]->head_data());
    }
  } else {
    for (uint32_t i = 0; i < cnt; i++) {
      received_bytes += batch.pkts()[i]->total_len();
    }
  }

  if (!(p->GetFlags() & DRIVER_FLAG_SELF_INC_STATS)) {
    p->queue_stats[PACKET_DIR_INC][qid].packets += cnt;
    p->queue_stats[PACKET_DIR_INC][qid].bytes += received_bytes;
  }

  RunNextModule(&batch);

  return {.block = false,
          .packets = cnt,
          .bits = (received_bytes + cnt * pkt_overhead) * 8};
}

CommandResponse QueueInc::CommandSetBurst(
    const bess::pb::QueueIncCommandSetBurstArg &arg) {
  if (arg.burst() > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "burst size must be [0,%zu]",
                          bess::PacketBatch::kMaxBurst);
  } else {
    burst_ = arg.burst();
    return CommandSuccess();
  }
}

ADD_MODULE(QueueInc, "queue_inc",
           "receives packets from a port via a specific queue")
