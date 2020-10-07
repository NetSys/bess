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

#include "port_out.h"
#include "../utils/format.h"

const Commands PortOut::cmds = {
    {"get_initial_arg", "EmptyArg", MODULE_CMD_FUNC(&PortOut::GetInitialArg),
     Command::THREAD_SAFE},
};

CommandResponse PortOut::Init(const bess::pb::PortOutArg &arg) {
  const char *port_name;
  int ret;

  if (!arg.port().length()) {
    return CommandFailure(EINVAL, "'port' must be given as a string");
  }

  port_name = arg.port().c_str();

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return CommandFailure(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;

  if (port_->num_queues[PACKET_DIR_OUT] == 0) {
    return CommandFailure(ENODEV, "Port %s has no outgoing queue", port_name);
  }

  ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                             PACKET_DIR_OUT, nullptr, 0);

  node_constraints_ = port_->GetNodePlacementConstraint();

  for (size_t i = 0; i < MAX_QUEUES_PER_DIR; i++) {
    mcs_lock_init(&queue_locks_[i]);
  }

  if (arg.rpfcheck()) {
    rpfcheck_ = 1;
  } else {
    rpfcheck_ = 0;
  }

  if (ret < 0) {
    return CommandFailure(-ret);
  }

  std::string attr_name = "ifIndex";
  using AccessMode = bess::metadata::Attribute::AccessMode;
  attr_id_ = AddMetadataAttr(attr_name, sizeof(uint64_t), AccessMode::kRead);

  return CommandSuccess();
}

CommandResponse PortOut::GetInitialArg(const bess::pb::EmptyArg &) {
  bess::pb::PortOutArg arg;
  arg.set_port(port_->name());
  arg.set_rpfcheck(rpfcheck_);
  return CommandSuccess(arg);
}

void PortOut::DeInit() {
  if (port_) {
    port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
                         nullptr, 0);
  }
}

std::string PortOut::GetDesc() const {
  return bess::utils::Format("%s/%s", port_->name().c_str(),
                             port_->port_builder()->class_name().c_str());
}

int PortOut::SendBatch(bess::PacketBatch *batch, queue_t qid) {
  uint64_t sent_bytes = 0;
  int sent_pkts = 0;
  int dropped = 0;
  int cnt =  batch->cnt();

  if (port_->conf().admin_up) {
    if ((!rpfcheck_) || (attr_id_ == -1)) {
        /* no RPF check - send everything at once */
        sent_pkts = port_->SendPackets(qid, batch->pkts(), batch->cnt());
    } else {
        bool need_flush = true;
        for (int i = 0; i < cnt; i++) {
          uint64_t ifIndex = 0;
          ifIndex = get_attr<uint64_t>(this, attr_id_, batch->pkts()[i]);
          if ((ifIndex > 0) && (ifIndex == port_->get_ifIndex())) {
            /* need a drop - send all packets till now,
             * free this packet */
            if (i - sent_pkts > 0) {
              sent_pkts +=
                  port_->SendPackets(qid, &batch->pkts()[sent_pkts], i - sent_pkts);
            }
            if (sent_pkts == i) {
              /* send sucessful, drop packet failing rpf check*/
              bess::Packet::Free(batch->pkts()[i]);
              /* Add dropped packet to "sent" count. We will subtract it later.
               */
              sent_pkts++; 
              dropped++;
              /* move to next packet */
            } else {
              /* remove from the total the length of packets we failed to send */
              for (int k = sent_pkts; k < i; k++) {
                sent_bytes -= batch->pkts()[k]->total_len();
              }
              need_flush = false;
              break;
            }
          } else {
              sent_bytes += batch->pkts()[i]->total_len();
          }
        }
        if (need_flush && (sent_pkts < cnt)) {
            sent_pkts += port_->SendPackets(qid, &batch->pkts()[sent_pkts], cnt - sent_pkts);
        }
     }
  }

  if (!(port_->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
    const packet_dir_t dir = PACKET_DIR_OUT;
    port_->queue_stats[dir][qid].packets += sent_pkts - dropped;
    port_->queue_stats[dir][qid].dropped += cnt - sent_pkts + dropped;
    port_->queue_stats[dir][qid].bytes += sent_bytes;
  }
  return sent_pkts;
}

void PortOut::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {

  CHECK(worker_queues_[ctx->wid] >= 0);
  queue_t qid = worker_queues_[ctx->wid];
  int sent_pkts = 0;


  if (queue_users_[qid] == 1) {
    sent_pkts = SendBatch(batch, qid);
  } else {
    mcslock_node_t me;
    mcs_lock(&queue_locks_[qid], &me);
    sent_pkts = SendBatch(batch, qid);
    mcs_unlock(&queue_locks_[qid], &me);
  }

  if (sent_pkts < batch->cnt()) {
    bess::Packet::Free(batch->pkts() + sent_pkts, batch->cnt() - sent_pkts);
  }
}

int PortOut::OnEvent(bess::Event e) {
  if (e != bess::Event::PreResume) {
    return -ENOTSUP;
  }

  const std::vector<bool> &actives = active_workers();
  int num_queues = port_->num_queues[PACKET_DIR_OUT];
  int next_queue = 0;

  for (int i = 0; i < MAX_QUEUES_PER_DIR; i++) {
    queue_users_[i] = 0;
  }

  for (size_t i = 0; i < Worker::kMaxWorkers; i++) {
    worker_queues_[i] = -1;
    if (actives[i]) {
      worker_queues_[i] = next_queue;
      queue_users_[next_queue]++;
      next_queue = (next_queue + 1) % num_queues;
    }
  }

  return 0;
}

ADD_MODULE(PortOut, "port_out", "sends pakets to a port")
