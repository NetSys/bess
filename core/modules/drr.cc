// Copyright (c) 2017, Joshua Stone.
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

#include "drr.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/udp.h"
#include "../utils/common.h"
#include "../utils/codel.h"

using bess::utils::Codel;
using bess::utils::LockLessQueue;
using bess::pb::DRRArg;

uint32_t RoundToPowerTwo(uint32_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

const Commands DRR::cmds = {
    {"set_quantum_size", "DRRQuantumArg",
     MODULE_CMD_FUNC(&DRR::CommandQuantumSize), Command::THREAD_UNSAFE},
    {"set_max_flow_queue_size", "DRRMaxFlowQueueSizeArg",
     MODULE_CMD_FUNC(&DRR::CommandMaxFlowQueueSize), Command::THREAD_UNSAFE}};

DRR::DRR()
    : quantum_(kDefaultQuantum),
      max_queue_size_(kFlowQueueMax),
      max_number_flows_(kDefaultNumFlows),
      flow_queue_(),
      current_flow_(nullptr), 
      codel_target_(0),
      codel_window_(0),
      init_queue_size_(0) {
        is_task_ = true;
      }
DRR::~DRR() {
  for (auto it = flows_.begin(); it != flows_.end();) {
    RemoveFlow(it->second);
    it++;
  }
}

CommandResponse DRR::Init(const DRRArg& arg) {
  CommandResponse err;
  task_id_t tid;

  if (arg.num_flows() != 0) {
    max_number_flows_ = RoundToPowerTwo(arg.num_flows() + 1);
  }

  if (arg.max_flow_queue_size() != 0) {
    err = SetMaxFlowQueueSize(arg.max_flow_queue_size());
    if (err.error().code() != 0) {
      return err;
    }
  }

  if (arg.quantum() != 0) {
    err = SetQuantumSize(arg.quantum());
    if (err.error().code() != 0) {
      return err;
    }
  }

  // register task
  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return CommandFailure(ENOMEM, "task creation failed");
  }

  // get flow queue arguments
  if (arg.has_codel()) {
    const DRRArg::Codel& codel = arg.codel();
    if (!(codel_target_ = codel.target())) {
      codel_target_ = Codel<bess::Packet*>::kDefaultTarget;
    }

    if (!(codel_window_ = codel.window())) {
      codel_window_ = Codel<bess::Packet*>::kDefaultWindow;
    }

  } else {
    const DRRArg::DropTailQueue& drop_queue = arg.queue();
     
    if (!(init_queue_size_ = drop_queue.size())) {
      init_queue_size_ = LockLessQueue<bess::Packet*>::kDefaultRingSize;
    }
  }

  return CommandSuccess();
}

CommandResponse DRR::CommandQuantumSize(const bess::pb::DRRQuantumArg& arg) {
  return SetQuantumSize(arg.quantum());
}

CommandResponse DRR::CommandMaxFlowQueueSize(
    const bess::pb::DRRMaxFlowQueueSizeArg& arg) {
  return SetMaxFlowQueueSize(arg.max_queue_size());
}

void DRR::ProcessBatch(bess::PacketBatch* batch) {
  int err = 0;

  // insert packets in the batch into their corresponding flows
  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet* pkt = batch->pkts()[i];

    // TODO(joshua): Add support for fragmented packets.
    FlowId id = GetId(pkt);
    auto it = flows_.Find(id);

    // if the Flow doesn't exist create one
    // and add the packet to the new Flow
    if (it == nullptr) {
      if (flow_queue_.size() >= max_number_flows_) {
        bess::Packet::Free(pkt);
      } else {
        AddNewFlow(pkt, id, &err);
        assert(err == 0);
      }
    } else {
      Enqueue(it->second, pkt, &err);
      assert(err == 0);
    }
  }
}

struct task_result DRR::RunTask(void*) {
  if (children_overload_ > 0) {
    return {
      .block = true,
      .packets = 0,
      .bits = 0,
    };
  }

  bess::PacketBatch batch;
  int err = 0;
  batch.clear();
  uint32_t total_bytes = GetNextBatch(&batch, &err);
  assert(err >= 0);  // TODO(joshua) do proper error checking

  if (total_bytes > 0) {
    RunNextModule(&batch);
  }

  // the number of bits inserted into the packet batch
  uint32_t cnt = batch.cnt();
  uint64_t bits_retrieved = (total_bytes + cnt * kPacketOverhead) * 8;
  return {.block = (cnt == 0), .packets = cnt, .bits = bits_retrieved};
}

uint32_t DRR::GetNextBatch(bess::PacketBatch* batch, int* err) {
  Flow* f;
  uint32_t total_bytes = 0;
  uint32_t count = flow_queue_.size();
  if (current_flow_) {
    count++;
  }
  int batch_size = batch->cnt();

  // iterate through flows in round robin fashion until batch is full
  while (!batch->full()) {
    // checks to see if there has been no update after a full round
    // ensures that if every flow is empty or if there are no flows
    // that will terminate with a non-full batch.
    if (count == 0) {
      if (batch_size == batch->cnt()) {
        break;
      } else {
        count = flow_queue_.size();
        batch_size = batch->cnt();
      }
    }
    count--;

    f = GetNextFlow(err);
    if (*err) {
      return total_bytes;
    } else if (f == nullptr) {
      continue;
    }

    uint32_t bytes = GetNextPackets(batch, f);
    total_bytes += bytes;

    if (f->queue->Empty() && !f->next_packet) {
      f->deficit = 0;
    }

    // if the flow doesn't have any more packets to give, reenqueue it
    if (!f->next_packet || f->next_packet->total_len() > f->deficit) {
        if (flow_queue_.size() < max_number_flows_) {
          flow_queue_.push_back(f);
        } else {
          *err = -1;
          return total_bytes;
        }
    } else {
      // knowing that the while statement will exit, keep the flow that still
      // has packets at the front
      current_flow_ = f;
    }
  }
  return total_bytes;
}

DRR::Flow* DRR::GetNextFlow(int* err) {
  Flow* f;
  double now = get_epoch_time();

  if (!current_flow_) {
    
    if (!flow_queue_.empty()) {
      f = flow_queue_.front();
      flow_queue_.pop_front();
    } else {
      *err = -1;
      return nullptr;
    }

    if (f->queue->Empty() && !f->next_packet) {
      // if the flow expired, remove it
      if (now - f->timer > kTtl) {
        RemoveFlow(f);
      } else {
        if (flow_queue_.size() < max_number_flows_) {
          flow_queue_.push_back(f);
        } else {
          *err = -1;
          return nullptr;
        }
      }
      return nullptr;
    }

    f->deficit += quantum_;
  } else {
    f = current_flow_;
    current_flow_ = nullptr;
  }
  return f;
}

uint32_t DRR::GetNextPackets(bess::PacketBatch* batch, Flow* f) {
  uint32_t total_bytes = 0;
  bess::Packet* pkt;

  while (!batch->full() && (!f->queue->Empty() || f->next_packet)) {
    // makes sure there isn't already a packet at the front
    if (!f->next_packet) {
      int err = f->queue->Pop(pkt);
      if (err) {
        return total_bytes;
      }
    } else {
      pkt = f->next_packet;
      f->next_packet = nullptr;
    }

    if (pkt->total_len() > f->deficit) {
      f->next_packet = pkt;
      break;
    }

    f->deficit -= pkt->total_len();
    total_bytes += pkt->total_len();
    batch->add(pkt);
  }

  return total_bytes;
}

DRR::FlowId DRR::GetId(bess::Packet* pkt) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;

  Ethernet* eth = pkt->head_data<Ethernet*>();
  Ipv4* ip = reinterpret_cast<Ipv4*>(eth + 1);
  size_t ip_bytes = ip->header_length << 2;
  Udp* udp = reinterpret_cast<Udp*>(reinterpret_cast<uint8_t*>(ip) +
                                    ip_bytes);  // Assumes a l-4 header
  // TODO(joshua): handle packet fragmentation
  FlowId id = {ip->src.value(), ip->dst.value(), udp->src_port.value(),
               udp->dst_port.value(), ip->protocol};
  return id;
}

void DRR::AddNewFlow(bess::Packet* pkt, FlowId id, int* err) {
  // creates flow
  Flow* f = new Flow(id);

  // TODO(joshua) do proper error checking
  if(!init_queue_size_) {
    f->queue = new Codel<bess::Packet*>(bess::Packet::Free, max_queue_size_, 
        codel_target_, codel_window_);
  } else {
    f->queue = new LockLessQueue<bess::Packet*>(init_queue_size_); 
  }

  flows_.Insert(id, f);

  Enqueue(f, pkt, err);
  if (*err) {
    return;
  }

  // puts flow in round robin
  if (flow_queue_.size() < max_number_flows_) {
    flow_queue_.push_back(f);
  } else {
    *err = -1;
  }
}

void DRR::RemoveFlow(Flow* f) {
  if (f == current_flow_) {
    current_flow_ = nullptr;
  }
  flows_.Remove(f->id);
  delete f;
}


void DRR::Enqueue(Flow* f, bess::Packet* newpkt, int* err) {
  // if the queue is full. drop the packet.
  if (f->queue->Size() >= max_queue_size_) {
    bess::Packet::Free(newpkt);
    return;
  }

  // creates a new queue if there is not enough space for the new packet
  // in the old queue
  if (f->queue->Full()) {
    uint32_t slots =
        RoundToPowerTwo(f->queue->Size() * kQueueGrowthFactor);
    *err = f->queue->Resize(slots);
    if (*err) {
      bess::Packet::Free(newpkt);
      return;
    }
  }

  *err = f->queue->Push(newpkt);
  if (!*err) {
    f->timer = get_epoch_time();
  } else {
    bess::Packet::Free(newpkt);
  }
}


CommandResponse DRR::SetQuantumSize(uint32_t size) {
  if (size == 0) {
    return CommandFailure(EINVAL, "quantum size must be at least 1");
  }

  quantum_ = size;
  return CommandSuccess();
}

CommandResponse DRR::SetMaxFlowQueueSize(uint32_t queue_size) {
  if (queue_size == 0) {
    return CommandFailure(EINVAL, "max queue size must be at least 1");
  }
  max_queue_size_ = queue_size;
  return CommandSuccess();
}

ADD_MODULE(DRR, "DRR", "Deficit Round Robin")
