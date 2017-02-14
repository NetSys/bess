#include "mlfqueue.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <time.h>
#include <string>
#include <cmath>
#include <iostream>
#include <fstream>

#include <glog/logging.h>

#include "../mem_alloc.h"

bool operator==(const MLFQueue::Flow_Id &id1, const MLFQueue::Flow_Id &id2) {
  bool ips = id1.src_ip == id2.src_ip && id1.dst_ip == id2.dst_ip;
  bool ports = id1.src_port == id2.src_port && id1.dst_port == id2.dst_port;
  return ips && ports && id1.protocol == id2.protocol;
}
bool operator<(const MLFQueue::Flow_Id &id1, const MLFQueue::Flow_Id &id2)  {
  bool ips = id1.src_ip < id2.src_ip && id1.dst_ip < id2.dst_ip;
  bool ports = id1.src_port < id2.src_port && id1.dst_port < id2.dst_port;
  return ips && ports && id1.protocol < id2.protocol;
}

int RoundToPowerTwo(uint32_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

const Commands MLFQueue::cmds = {
    {"set_num_levels", "MlfqLevelArg", MODULE_CMD_FUNC(&MLFQueue::CommandNumPriorityLevels), 0},
    {"set_batch_Size", "MlfqBatchArg", MODULE_CMD_FUNC(&MLFQueue::CommandBatchSize), 0},
    {"set_max_flow_queue_size", "MlfqMaxFlowQueueSizeArg",
          MODULE_CMD_FUNC(&MLFQueue::CommandMaxFlowQueueSize), 0}};

pb_error_t MLFQueue::Init(const bess::pb::MlfqArg &arg) {
  pb_error_t err;
  task_id_t tid;
  if (arg.num_levels() != 0) {
    err = SetNumPriorityLevels(arg.num_levels());
    if (err.err() != 0)  {
      return err;
    }
  } else {
    err = SetNumPriorityLevels(kDEFAULT_NUM_LEVELS);
    if (err.err() != 0) {
      return err;
    }
  }

  if (arg.max_flow_queue_size() != 0) {
    err = SetMaxFlowQueueSize(arg.max_flow_queue_size());
    if (err.err() != 0) {
      return err;
    }
  } else {
    err = SetMaxFlowQueueSize(kFLOW_QUEUE_MAX);
    if (err.err() != 0) {
      return err;
    }
  }

  if (arg.batch_size() != 0) {
    err = SetBatchSize(arg.batch_size());
    if (err.err() != 0) {
      return err;
    }
  } else {
    err = SetBatchSize(kDEFAULT_BATCH_SIZE);
    if (err.err() != 0) {
      return err;
    }
  }

  if(arg.init_load() != 0) {
    load_avg_ = arg.init_load();
  } else {
    load_avg_ = kINITIAL_LOAD;
  }

  /* register task */
  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return pb_error(ENOMEM, "task creation failed");
  }

  init_flow_size_ =  RoundToPowerTwo(kFLOW_QUEUE_FACTOR*batch_size_);
  ready_flows_ = 0;
  return pb_errno(0);
}

void MLFQueue::DeInit() {
  bess::Packet *pkt;

  for (auto it=flows_.begin(); it!=flows_.end(); ) {
    Flow *f = &it->second;
    if (f->queue) {
      while (llring_sc_dequeue(f->queue, (void **)&pkt) == 0) {
        bess::Packet::Free(pkt);
      }
      mem_free(f->queue);
    }
    it++;
  }
}

pb_cmd_response_t MLFQueue::CommandNumPriorityLevels(const bess::pb::MlfqLevelArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetNumPriorityLevels(arg.num_levels()));
  return response;
}

pb_cmd_response_t MLFQueue::CommandBatchSize(const bess::pb::MlfqBatchArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetBatchSize(arg.batch_size()));
  return response;
}

pb_cmd_response_t MLFQueue::CommandMaxFlowQueueSize(const bess::pb::MlfqMaxFlowQueueSizeArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetMaxFlowQueueSize(arg.max_queue_size()));
  return response;
}

void MLFQueue::ProcessBatch(bess::PacketBatch *batch) {
  //insert packets in the batch into their corresponding flows
  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *pkt = batch->pkts()[i];
    Flow_Id id = MLFQueue::GetId(pkt);// TODO(joshua): Add support for fragmented packets.
    auto it = flows_.find(id);

    //if the Flow doesn't exist create one
    //and add the packet to the new Flow
    if (it == flows_.end()) {
      Flow f = {(float) max_level_, 0, 0, 0};
      int err = MLFQueue::AddQueue(&f, init_flow_size_);//TODO(joshua) do proper error checking
      assert(err == 0);
      assert(f.queue);
      err++;

      Enqueue(&f, pkt);
      InsertFlow(&f);
      ready_flows_ += 1;
    } else {
      Enqueue(&it->second, pkt);
    }
  }
}

struct task_result MLFQueue::RunTask(void *) {
  bess::PacketBatch batch;
  struct task_result ret;

  //the highest priority level with flows.
  GetNextBatch(&batch);

  uint64_t cnt = batch.cnt();
  if (cnt > 0) {
    RunNextModule(&batch);
    //after processing a getbatch call, updates flows
    //and their priority location
    UpdateAllFlows();
  }

  uint64_t total_bytes = 0;
  for (uint32_t i = 0; i < cnt; i++)
    total_bytes += batch.pkts()[i]->total_len();
  ret = (struct task_result){
      .packets = cnt, .bits = (total_bytes + cnt * kPACKET_OVERHEAD) * 8,
  };

  return ret;
}

void MLFQueue::GetNextBatch(bess::PacketBatch* batch) {
  batch->clear();

  int batch_left = batch_size_;
  int level = max_level_;

  //iterate through the priority levels starting at the top_level
  //and dequeue packets for batch equally across each level.
  while(batch_left > 0 && level >= 0) {
    if(levels_[level].empty()) {
      level--;
      continue;
    }

    int flow_max = (batch_left)/levels_[level].size();
    for(unsigned int i= 0; i < levels_[level].size(); i++) {
      int num_pkts = AddtoBatch(batch, levels_[level][i], flow_max);
      batch_left -= num_pkts;

      if (num_pkts < flow_max) {
        flow_max = (batch_left)/(levels_[level].size()-i + 1);
      }
      levels_[level][i]->throughput += num_pkts;
    }
    level--;
  }
}


int MLFQueue::AddtoBatch(bess::PacketBatch *batch, Flow* f, int slots) {
  int prev = batch->cnt();
  int cnt = llring_dequeue_burst(f->queue, (void **)batch->pkts(), slots);
  if(cnt > 0) {
    batch->set_cnt(prev+cnt);
  }
  return cnt;
}

MLFQueue::Flow_Id MLFQueue::GetId(bess::Packet* pkt) {
  struct ether_hdr *eth = pkt->head_data<struct ether_hdr *>();
  struct ipv4_hdr *ip = reinterpret_cast<struct ipv4_hdr *>(eth + 1);
  int ip_bytes = (ip->version_ihl & 0xf) << 2;
  struct udp_hdr *udp = reinterpret_cast<struct udp_hdr *>(
      reinterpret_cast<uint8_t *>(ip) + ip_bytes);// Assumes a l-4 header
  //TODO(joshua): handle packet fragmentation
  struct Flow_Id id = {ip->src_addr, ip->dst_addr, udp->src_port,
      udp->dst_port, ip->next_proto_id};
  return id;
}

int MLFQueue::AddQueue(Flow* f, int slots) {
  struct llring *new_queue;

  int bytes = llring_bytes_with_slots(slots);
  volatile int ret;

  new_queue = static_cast<llring *>(mem_alloc_ex(bytes, alignof(llring), 0));
  if (!new_queue) {
    return -ENOMEM;
  }

  ret = llring_init(new_queue, slots, 1, 1);
  if (ret) {
    mem_free(new_queue);
    return -EINVAL;
  }
  f->queue = new_queue;
  return 0;
}

int MLFQueue::Enqueue(Flow* f, bess::Packet* newpkt) {
  int ret;

  //creates a new queue if there is not enough space for the new packet
  //in the old queue
  if(llring_full(f->queue)) {
    if(llring_count(f->queue)*kQUEUE_GROWTH_FACTOR > max_queue_size_) {
      bess::Packet::Free(newpkt);
      return 0;
    }
    struct llring *old_queue = f->queue;
    ret = AddQueue(f, llring_count(f->queue)*kQUEUE_GROWTH_FACTOR);
    if(ret != 0) {
      return ret;
    }

    //migrate packets from the old queue
    if (old_queue) {
      bess::Packet *pkt;

      while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
        ret = llring_sp_enqueue(f->queue, pkt);
        if (ret == -LLRING_ERR_NOBUF) {
          bess::Packet::Free(pkt);
        }
      }

      mem_free(old_queue);
    }
  }
  if(llring_empty(f->queue)) {
    ready_flows_++;
  }
  ret = llring_sp_enqueue(f->queue, newpkt);
  time(&f->timer);
  return ret;
}

void MLFQueue::UpdateFlow(Flow* f) {
  f->throughput = ((2.0*load_avg_)/(2.0*load_avg_ + 1))*f->throughput;
  f->priority = max_level_ - f->throughput/4.0;
}

void MLFQueue::UpdateAllFlows() {
  levels_.clear();
  levels_.resize(max_level_+1);
  time_t now;
  time(&now);

  // iterate through all flows and recalculate their priority
  //and assign them to the corresponding priority level
  for (auto it=flows_.begin(); it!=flows_.end(); ) {
      Flow *f = &it->second;

      if(llring_empty(f->queue)) {
        ready_flows_ -= 1;

        //if the flow expired, remove it
        if(difftime(f->timer, now) > kTTL) {
          mem_free(f->queue);
          flows_.erase(it++);
          continue;
        }
      }
      else UpdateFlow(f);
      InsertFlow(f);
      ++it;
  }
  //having a newly calibrated ready flows, updates the load average
  load_avg_ = (max_level_ -1)/((float) max_level_) + (1.0/max_level_)*ready_flows_;
}

void MLFQueue::InsertFlow(Flow* f) {
  int level = (int) f->priority;
  levels_[level].push_back(f);
}

void MLFQueue::Resize(uint8_t num_levels) {
  if(num_levels - 1 == max_level_) {
     return;
  }
  max_level_ = num_levels - 1;
  UpdateAllFlows();

}

pb_error_t MLFQueue::SetNumPriorityLevels(uint32_t num_levels) {
  if (num_levels == 0 || num_levels > 255) {
    return pb_error(EINVAL, "must be in [1, 255]");
  }

  Resize(num_levels);
  return pb_errno(0);
}

pb_error_t MLFQueue::SetBatchSize(uint32_t size) {
  if (size == 0 ||
      size > static_cast<uint32_t>(bess::PacketBatch::kMaxBurst)) {
    return pb_error(EINVAL, "batch size must be [1,%lu]",
                    bess::PacketBatch::kMaxBurst);
  }

  batch_size_ = size;
  init_flow_size_ = RoundToPowerTwo(kFLOW_QUEUE_FACTOR*batch_size_);
  return pb_errno(0);
}

pb_error_t MLFQueue::SetMaxFlowQueueSize(uint32_t queue_size) {
  if(queue_size == 0) {
    return pb_error(EINVAL, "max queue size must be at least 1");
  }
  max_queue_size_ = queue_size;
  return pb_errno(0);
}

ADD_MODULE(MLFQueue, "MLFQueue", "Multi-Level Feedback Queue")
