#ifndef BESS_MODULES_MLFQUEUE_H_
#define BESS_MODULES_MLFQUEUE_H_

#include <vector>
#include <map>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/ip.h"
#include "../pktbatch.h"
#include "../kmod/llring.h"


using bess::utils::IPAddress;
using bess::utils::CIDRNetwork;

#define DEFAULT_BATCH_SIZE 20 //the batch size used if non specified
#define DEFAULT_NUM_LVLS 10 //the priority levels used if non specified
#define FLOW_QUEUE_FACTOR 200 //multiplier on batch_size for initial queue size for a flow
#define QUEUE_GROWTH_FACTOR 2 //the scale at which the flow's queue size grows
#define INITIAL_LOAD 50 //the packet load used if non specified
#define TTL 300 //time to live for flow entries

// bool LessThanCIDR(const CIDRNetwork &ip1, const CIDRNetwork &ip2) {
//   return (ip1.addr & ip1.mask) < (ip2.addr & ip2.mask);
// }

class MLFQueue final : public Module {
 public:

  static const Commands cmds;

  pb_error_t Init(const bess::pb::MlfqArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandNumPriorityLevels(const bess::pb::MlfqLvlArg &arg);
  pb_cmd_response_t CommandBatchSize(const bess::pb::MlfqBatchArg &arg);

  struct Q_Id {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t src_port;
    uint32_t dst_port;
    uint8_t protocol;

  };

  struct Flow {
    float priority;
    float throughput;
    time_t timer;
    struct llring *queue;
  };

 private:
   pb_error_t SetNumPriorityLevels(uint32_t num_lvls);
   pb_error_t SetBatchSize(uint32_t batch_size);
   void Resize(uint8_t num_lvls);
   int Enqueue(Flow* f, void* pkt);

   Q_Id GetId(bess::Packet* p);
   int FindTopLevel();

   void UpdateFlow(Flow* f, bool update_pkts);
   void UpdateAllFlows(bool update_pkts, int top_lvl);
   void InsertFlow(Flow* f);

   bess::PacketBatch GetNextBatch();
   int AddtoBatch(bess::PacketBatch *batch, Flow* f, int slots);
   int AddQueue(Flow* f, int slots);

   const uint8_t min_lvl_ = 0;
   uint8_t max_lvl_;
   float load_avg_;
   int ready_flows_;
   uint32_t batch_size_;
   std::map<Q_Id, Flow> flows_;
   std::vector<std::vector<Flow*>> levels_;

};

#endif  // BESS_MODULES_MLFQUEUE_H_
