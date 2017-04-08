#include "drr.h"

#include <glog/logging.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <time.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

#include "../utils/common.h"

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
     MODULE_CMD_FUNC(&DRR::CommandQuantumSize), 0},
    {"set_max_flow_queue_size", "DRRMaxFlowQueueSizeArg",
     MODULE_CMD_FUNC(&DRR::CommandMaxFlowQueueSize), 0}};

DRR::DRR()
    : quantum_(kDefaultQuantum),
      max_queue_size_(kFlowQueueMax),
      max_number_flows_(kDefaultNumFlows),
      flow_ring_(nullptr),
      current_flow_(nullptr) {}

DRR::~DRR() {
  for (auto it = flows_.begin(); it != flows_.end();) {
    RemoveFlow(it->second);
    it++;
  }
  std::free(flow_ring_);
}

pb_error_t DRR::Init(const bess::pb::DRRArg& arg) {
  pb_error_t err;
  task_id_t tid;

  if (arg.num_flows() != 0) {
    max_number_flows_ = RoundToPowerTwo(arg.num_flows() + 1);
  }

  if (arg.max_flow_queue_size() != 0) {
    err = SetMaxFlowQueueSize(arg.max_flow_queue_size());
    if (err.err() != 0) {
      return err;
    }
  }

  if (arg.quantum() != 0) {
    err = SetQuantumSize(arg.quantum());
    if (err.err() != 0) {
      return err;
    }
  }

  /* register task */
  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return pb_error(ENOMEM, "task creation failed");
  }

  int err_num = 0;
  flow_ring_ = AddQueue(max_number_flows_, &err_num);
  if (err_num != 0) {
    return pb_errno(err_num);
  }

  return pb_errno(0);
}

pb_cmd_response_t DRR::CommandQuantumSize(const bess::pb::DRRQuantumArg& arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetQuantumSize(arg.quantum()));
  return response;
}

pb_cmd_response_t DRR::CommandMaxFlowQueueSize(
    const bess::pb::DRRMaxFlowQueueSizeArg& arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetMaxFlowQueueSize(arg.max_queue_size()));
  return response;
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
      if (llring_full(flow_ring_)) {
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
  bess::PacketBatch batch;
  struct task_result ret;
  int err = 0;
  batch.clear();
  uint32_t total_bytes = 0;
  if (flow_ring_ != NULL) {
    total_bytes = GetNextBatch(&batch, &err);
  }
  assert(err >= 0);  // TODO(joshua) do proper error checking

  if (total_bytes > 0) {
    RunNextModule(&batch);
  }

  // the number of bits inserted into the packet batch
  uint64_t cnt = batch.cnt();
  uint64_t bits_retrieved = (total_bytes + cnt * kPacketOverhead) * 8;
  ret = (struct task_result){.packets = cnt, .bits = bits_retrieved};

  return ret;
}

uint32_t DRR::GetNextBatch(bess::PacketBatch* batch, int* err) {
  Flow* f;
  uint32_t total_bytes = 0;
  uint32_t count = llring_count(flow_ring_);
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
        count = llring_count(flow_ring_);
        batch_size = batch->cnt();
      }
    }
    count--;

    f = GetNextFlow(err);
    if (*err != 0) {
      return total_bytes;
    } else if (f == nullptr) {
      continue;
    }

    uint32_t bytes = GetNextPackets(batch, f, err);
    total_bytes += bytes;
    if (*err != 0) {
      return total_bytes;
    }

    if (llring_empty(f->queue) && !f->next_packet) {
      f->deficit = 0;
    }

    // if the flow doesn't have any more packets to give, reenqueue it
    if (!f->next_packet || f->next_packet->total_len() > f->deficit) {
      *err = llring_enqueue(flow_ring_, f);
      if (*err != 0) {
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
    *err = llring_dequeue(flow_ring_, reinterpret_cast<void**>(&f));
    if (*err < 0) {
      return nullptr;
    }

    if (llring_empty(f->queue) && !f->next_packet) {
      // if the flow expired, remove it
      if (now - f->timer > kTtl) {
        RemoveFlow(f);
      } else {
        *err = llring_enqueue(flow_ring_, f);
        if (*err < 0) {
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

uint32_t DRR::GetNextPackets(bess::PacketBatch* batch, Flow* f, int* err) {
  uint32_t total_bytes = 0;
  bess::Packet* pkt;

  while (!batch->full() && (!llring_empty(f->queue) || f->next_packet)) {
    // makes sure there isn't already a packet at the front
    if (!f->next_packet) {
      *err = llring_dequeue(f->queue, reinterpret_cast<void**>(&pkt));
      if (*err < 0) {
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
  struct ether_hdr* eth = pkt->head_data<struct ether_hdr*>();
  struct ipv4_hdr* ip = reinterpret_cast<struct ipv4_hdr*>(eth + 1);
  int ip_bytes = (ip->version_ihl & 0xf) << 2;
  struct udp_hdr* udp = reinterpret_cast<struct udp_hdr*>(
      reinterpret_cast<uint8_t*>(ip) + ip_bytes);  // Assumes a l-4 header
  // TODO(joshua): handle packet fragmentation
  FlowId id = {ip->src_addr, ip->dst_addr, udp->src_port, udp->dst_port,
               ip->next_proto_id};
  return id;
}

void DRR::AddNewFlow(bess::Packet* pkt, FlowId id, int* err) {
  // creates flow
  Flow* f = new Flow(id);

  // TODO(joshua) do proper error checking
  f->queue = AddQueue(static_cast<int>(kFlowQueueSize), err);

  if (*err != 0) {
    return;
  }

  flows_.Insert(id, f);

  Enqueue(f, pkt, err);
  if (*err != 0) {
    return;
  }

  // puts flow in round robin
  *err = llring_enqueue(flow_ring_, f);
}

void DRR::RemoveFlow(Flow* f) {
  if (f == current_flow_) {
    current_flow_ = nullptr;
  }
  flows_.Remove(f->id);
  delete f;
}

llring* DRR::AddQueue(uint32_t slots, int* err) {
  int bytes = llring_bytes_with_slots(slots);
  int ret;

  llring* queue = static_cast<llring*>(aligned_alloc(alignof(llring), bytes));
  if (!queue) {
    *err = -ENOMEM;
    return nullptr;
  }

  ret = llring_init(queue, slots, 1, 1);
  if (ret) {
    std::free(queue);
    *err = -EINVAL;
    return nullptr;
  }
  return queue;
}

void DRR::Enqueue(Flow* f, bess::Packet* newpkt, int* err) {
  // if the queue is full. drop the packet.
  if (llring_count(f->queue) >= max_queue_size_) {
    bess::Packet::Free(newpkt);
    return;
  }

  // creates a new queue if there is not enough space for the new packet
  // in the old queue
  if (llring_full(f->queue)) {
    uint32_t slots =
        RoundToPowerTwo(llring_count(f->queue) * kQueueGrowthFactor);
    f->queue = ResizeQueue(f->queue, slots, err);
    if (*err != 0) {
      return;
    }
  }

  *err = llring_enqueue(f->queue, reinterpret_cast<void*>(newpkt));
  if (*err == 0) {
    f->timer = get_epoch_time();
  }
}

llring* DRR::ResizeQueue(llring* old_queue, uint32_t new_size, int* err) {
  llring* new_queue = AddQueue(new_size, err);
  if (*err != 0) {
    return nullptr;
  }

  // migrates packets from the old queue
  if (old_queue) {
    bess::Packet* pkt;

    while (llring_dequeue(old_queue, reinterpret_cast<void**>(&pkt)) == 0) {
      *err = llring_enqueue(new_queue, pkt);
      if (*err == -LLRING_ERR_NOBUF) {
        bess::Packet::Free(pkt);
        *err = 0;
      } else if (*err != 0) {
        std::free(new_queue);
        return nullptr;
      }
    }

    std::free(old_queue);
  }
  return new_queue;
}

pb_error_t DRR::SetQuantumSize(uint32_t size) {
  if (size == 0) {
    return pb_error(EINVAL, "quantum size must be at least 1");
  }

  quantum_ = size;
  return pb_errno(0);
}

pb_error_t DRR::SetMaxFlowQueueSize(uint32_t queue_size) {
  if (queue_size == 0) {
    return pb_error(EINVAL, "max queue size must be at least 1");
  }
  max_queue_size_ = queue_size;
  return pb_errno(0);
}

ADD_MODULE(DRR, "DRR", "Deficit Round Robin")
