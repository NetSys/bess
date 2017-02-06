#include "mlfqueue.h"
#include "../mem_alloc.h"

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <time.h>

#include <glog/logging.h>

#include <string>
#include <cmath>
#include <iostream>
#include <fstream>

bool operator==(const MLFQueue::Q_Id &id1, const MLFQueue::Q_Id &id2) {
  bool ips = id1.src_ip == id2.src_ip && id1.dst_ip == id2.dst_ip;
  bool ports = id1.src_port == id2.src_port && id1.dst_port == id2.dst_port;
  return ips && ports && id1.protocol == id2.protocol;
}
bool operator<(const MLFQueue::Q_Id &id1, const MLFQueue::Q_Id &id2)  {
  bool ips = id1.src_ip < id2.src_ip && id1.dst_ip < id2.dst_ip;
  bool ports = id1.src_port < id2.src_port && id1.dst_port < id2.dst_port;
  return ips && ports && id1.protocol < id2.protocol;
}

int RoundToPowerTwo(int val) {
 return pow(2, ceil(log2(val)));
}

const Commands MLFQueue::cmds = {
    {"set_num_lvls", "MlfqLvlArg", MODULE_CMD_FUNC(&MLFQueue::CommandNumPriorityLevels), 0},
    {"set_batch_Size", "MlfqBatchArg", MODULE_CMD_FUNC(&MLFQueue::CommandBatchSize), 0},
    {"set_max_flow_queue_size", "MlfqMaxFlowQueueSizeArg",
          MODULE_CMD_FUNC(&MLFQueue::CommandMaxFlowQueueSize), 0}};

pb_error_t MLFQueue::Init(const bess::pb::MlfqArg &arg) {
  pb_error_t err;

  if (arg.num_lvls() != 0) {
    err = SetNumPriorityLevels(arg.num_lvls());
    if (err.err() != 0) {
      return err;
    }
  } else {
    err = SetNumPriorityLevels(DEFAULT_NUM_LVLS);
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
    err = SetMaxFlowQueueSize(FLOW_QUEUE_MAX);
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
    err = SetBatchSize(DEFAULT_BATCH_SIZE);
    if (err.err() != 0) {
      return err;
    }
  }

  if(arg.init_load() != 0) {
    load_avg_ = arg.init_load();
  } else {
    load_avg_ = INITIAL_LOAD;
  }
  ready_flows_ = 0;
  return pb_errno(0);
}

void MLFQueue::DeInit() {
  bess::Packet *pkt;

  for (std::map<Q_Id, Flow>::iterator it=flows_.begin(); it!=flows_.end(); ) {
    Flow *f = &it->second;
    if (f->queue) {
      while (llring_sc_dequeue(f->queue, (void **)&pkt) == 0) {
        bess::Packet::Free(pkt);
      }
      mem_free(f->queue);
    }
  }
}

pb_cmd_response_t MLFQueue::CommandNumPriorityLevels(const bess::pb::MlfqLvlArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetNumPriorityLevels(arg.num_lvls()));
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
  google::SetLogDestination(google::GLOG_INFO,"/vagrant/batch.log");
  LOG(INFO)<< std::to_string(batch->cnt());
  //insert packets in the batch into their corresponding flows
  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *pkt = batch->pkts()[i];
    Q_Id id = MLFQueue::GetId(pkt);//doesn't handle fragmented packets
    std::map<Q_Id, Flow>::iterator it = flows_.find(id);

    //if the Flow doesn't exist create one
    //and add the packet to the new Flow
    if (it == flows_.end()) {
      Flow f = {(float) max_lvl_, 0, 0, 0};
      int slots = RoundToPowerTwo(FLOW_QUEUE_FACTOR*batch_size_);
      int err = MLFQueue::AddQueue(&f, slots);
      assert(err == 0);
      assert(f->queue);
      err += 1;

      Enqueue(&f, pkt);
      InsertFlow(&f);
      ready_flows_ += 1;
    } else {
      Enqueue(&it->second, pkt);
    }
  }
  bess::PacketBatch next_batch = GetNextBatch();
  RunNextModule(&next_batch);
}

//obtain the next batch of packets from the highest priority level's Flows.
bess::PacketBatch MLFQueue::GetNextBatch() {
  bess::PacketBatch batch;
  batch.clear();

  //the highest priority level with flows.
  int lvl = FindTopLevel();
  if(levels_[lvl].size() == 0) return batch;
  //the current allocated section of the batch per each flow
  int flow_max = batch_size_/levels_[lvl].size();
  int enqueued = 0;

  //iterate through the highest priority level's flows
  //and dequeue packets for batch
  for(unsigned int i= 0; i < levels_[lvl].size(); i++) {
    int num_pkts = AddtoBatch(&batch, levels_[lvl][i], flow_max);
    enqueued += num_pkts;

    if (num_pkts < flow_max) {
      flow_max = (batch_size_-enqueued)/(levels_[lvl].size()-i + 1);
    }
    levels_[lvl][i]->throughput += num_pkts;
  }

  //after processing a batch calls for an update flows
  //and update their priority location
  UpdateAllFlows(false, lvl);
  return batch;
}

//dequeues from the specified slots from the flow f into the Packetbatch
int MLFQueue::AddtoBatch(bess::PacketBatch *batch, Flow* f, int slots) {
  int prev = batch->cnt();
  int cnt = llring_dequeue_burst(f->queue, (void **)batch->pkts(), slots);
  if(cnt > 0) batch->set_cnt(prev+cnt);
  return cnt;
}

//Find the first priority level with Flows in ascending order
int MLFQueue::FindTopLevel() {
  for(int i = max_lvl_; i >= 0; i--) {
    if(!levels_[i].empty()) return i;
  }
  return 0;
}

// gets the 5 element identifier for a flow given a packet
MLFQueue::Q_Id MLFQueue::GetId(bess::Packet* pkt) {
      struct ether_hdr *eth = pkt->head_data<struct ether_hdr *>();
      struct ipv4_hdr *ip = reinterpret_cast<struct ipv4_hdr *>(eth + 1);
      int ip_bytes = (ip->version_ihl & 0xf) << 2;
      struct udp_hdr *udp = reinterpret_cast<struct udp_hdr *>(
          reinterpret_cast<uint8_t *>(ip) + ip_bytes);// Assumes a l-4 header
      //TO-DO: handle packet fragmentation
      struct Q_Id id = {ip->src_addr, ip->dst_addr, udp->src_port,
          udp->dst_port, ip->next_proto_id};
      return id;
}

//allocates llring queue space and adds the queue to the specified flow with
//size indicated by slots
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
    if(llring_count(f->queue)*QUEUE_GROWTH_FACTOR > max_queue_size_) {
      bess::Packet::Free(newpkt);
      return 0;
    }
    struct llring *old_queue = f->queue;
    ret = AddQueue(f, llring_count(f->queue)*QUEUE_GROWTH_FACTOR);
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
  ret = llring_sp_enqueue(f->queue, newpkt);
  time(&f->timer);
  return ret;
}

/*updates the metrics that determine the priority level of the flow
*update_pkts is set to false in the case of recently
* forwarded packets that have already resulted in a throughput update
*/
void MLFQueue::UpdateFlow(Flow* f, bool update_pkts=true) {
  if(update_pkts) {
    f->throughput = ((2.0*load_avg_)/(2.0*load_avg_ + 1))*f->throughput;
  }
  f->priority = max_lvl_ - f->throughput/4.0;
}

//top_lvl is the location of the highest priority flows and update_pkts
//indicates whether these flows still need their throughput updated in UpdateFlow
void MLFQueue::UpdateAllFlows(bool update_pkts=true, int top_lvl=0) {
  levels_.clear();
  levels_.resize(max_lvl_+1);
  time_t now;
  time(&now);
  // iterate through all flows and recalculate their priority
  //and assign them to the corresponding priority level
  for (std::map<Q_Id, Flow>::iterator it=flows_.begin(); it!=flows_.end(); ) {
      Flow *f = &it->second;

      if(llring_empty(f->queue)) {
        ready_flows_ -= 1;

        //if the flow expired, remove it
        if(difftime(f->timer, now) > TTL) {
          mem_free(f->queue);
          flows_.erase(it++);
          continue;
        }
      }

      if(top_lvl == (int)f->priority) UpdateFlow(f, update_pkts);
      else UpdateFlow(f);
      InsertFlow(f);
      ++it;
  }
  //having a newly calibrated ready flows, updates the load average
  load_avg_ = (max_lvl_ -1)/((float) max_lvl_) + (1.0/max_lvl_)*ready_flows_;
}

void MLFQueue::InsertFlow(Flow* f) {
  int lvl = (int) f->priority;
  levels_[lvl].push_back(f);
}

void MLFQueue::Resize(uint8_t num_lvls) {
  if(num_lvls - 1 == max_lvl_) { return; }
  max_lvl_ = num_lvls - 1;
  UpdateAllFlows();

}

pb_error_t MLFQueue::SetNumPriorityLevels(uint32_t num_lvls) {
  if (num_lvls == 0 || num_lvls > 255) {
    return pb_error(EINVAL, "must be in [1, 255]");
  }

  Resize(num_lvls);
  return pb_errno(0);
}

pb_error_t MLFQueue::SetBatchSize(uint32_t size) {
  if (size == 0 ||
      size > static_cast<uint32_t>(bess::PacketBatch::kMaxBurst)) {
    return pb_error(EINVAL, "batch size must be [1,%lu]",
                    bess::PacketBatch::kMaxBurst);
  }

  batch_size_ = size;
  return pb_errno(0);
}

pb_error_t MLFQueue::SetMaxFlowQueueSize(uint32_t queue_size) {
  if(queue_size == 0) {
    return pb_error(EINVAL, "max queue size must be atleast 1");
  }
  max_queue_size_ = queue_size;
  return pb_errno(0);
}

ADD_MODULE(MLFQueue, "MLFQueue", "Multi-Level Feedback Queue")
