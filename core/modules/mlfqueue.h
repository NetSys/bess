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

/*
  * The Multi-Level Feedback Queue module queues from one input gate to one
    output gate without any packet modification. Queue is set up as having specified
    number of priority levels where each Flow will be assigned based on its priority.
    The priority for each flow is calculated as a function of Flow throughput
    and average load of the Module. After receiving a batch to process, in which
    each packet is assigned to a flow based on their 5 tuple, The module will
    find the highest priority level and split the current return batch size
    between the all of this levels flows evenly. The module will then query those
    flows for packets based on this evenly split batch size. The module will then
    return this new packet batch.

    based on this idea:
      https://en.wikipedia.org/wiki/Multilevel_feedback_queue
  * EXPECTS: Input packets in any format
  *
  * MODIFICATIONS: None
  *
  * INPUT GATES: 1
  *
  * OUTPUT GATES: 1
  *
  * PARAMETERS:
  *    * Batch size: the size of the output batches
  *    * Number of levels: Number of levels in the Multi-Level Queue
  *    * Initial Load: the predicted average load of the module as a initial
  *          base to calculate priorities
  *    * Max Flow Queue Size: the maximum size that any Flows queue can get before
  *          the module will start dropping the flows packets
  * COMMANDS
  *    update Batch Size: can be done live
  *    update Number of levels: cannot not be done live
  *    update Max Flow Queue Size: can be done live
*/
class MLFQueue final : public Module {
 public:
   #define kDEFAULT_BATCH_SIZE 20 //the batch size used if non specified
   #define kDEFAULT_NUM_LEVELS 10 //the priority levels used if non specified
   #define kFLOW_QUEUE_FACTOR 100 //multiplier on batch_size for initial queue size for a flow
   #define kQUEUE_GROWTH_FACTOR 2 //the scale at which the flow's queue size grows
   #define kFLOW_QUEUE_MAX 200000//the max flow queue size if non-specified
   #define kINITIAL_LOAD 50 //the initial packet load used if non specified
   #define kTTL 300 //time to live for flow entries
   #define kPACKET_OVERHEAD 24 //used to calculate the total output of packets for runtask

   //5 tuple id to identify a flow from a packet header information.
   struct Flow_Id {
     uint32_t src_ip;
     uint32_t dst_ip;
     uint32_t src_port;
     uint32_t dst_port;
     uint8_t protocol;

   };

   /*
    stores the metrics of the flow, a timer and the queue to store the packets in.
   */
   struct Flow {
     float priority;// used to determine the level of priority
     float throughput;//sliding average number of packets for flow
     time_t timer;// to determine if TTL should be used
     struct llring *queue;// queue to store current packets for flow
   };

   static const Commands cmds;

   pb_error_t Init(const bess::pb::MlfqArg &arg);

   void DeInit() override;

   void ProcessBatch(bess::PacketBatch *batch) override;

   struct task_result RunTask(void *) override;

   pb_cmd_response_t CommandNumPriorityLevels(const bess::pb::MlfqLevelArg &arg);
   pb_cmd_response_t CommandBatchSize(const bess::pb::MlfqBatchArg &arg);
   pb_cmd_response_t CommandMaxFlowQueueSize(const bess::pb::MlfqMaxFlowQueueSizeArg &arg);


 private:

   /*
    sets Number of levels in the Multi-Level Queue
    Args:
      num_levels: the value to set the number of levels to
   */
   pb_error_t SetNumPriorityLevels(uint32_t num_levels);

   /*
    sets the size of the output batches
    Args:
      batch_size: the size to set it to.
   */
   pb_error_t SetBatchSize(uint32_t batch_size);

   /*
    sets the maximum size that any Flows queue can get before the module will start
    dropping the flows packets.
    Args:
      queue-size: the maximum size for any Flows queue.
    */
   pb_error_t SetMaxFlowQueueSize(uint32_t queue_size);

   /*
    resizes the Multi-Level queue to the speciifed size. then calls UpdateFlow,
    to update all of the flows' metrics and reinsert them into the queue.
    Args:
      num_levels: the number of levels to have in the queue.
   */
   void Resize(uint8_t num_levels);

   /*
    puts the packet into the llring queue within the flow
    Args:
      f: the flow to enqueue the packet into
      pkt: the packet to enqueue into the flow's queue
   */
   int Enqueue(Flow* f, bess::Packet* pkt);

   /*
    Args:
      p: a Packet to get id for
    Returns:
      the 5 element identifier for the flow that the packet belongs to
    */
   Flow_Id GetId(bess::Packet* p);

   /*
    updates the metrics that determine the priority level of the flow
    Args:
      f: the flow to update the metrics for
   */
   void UpdateFlow(Flow* f);

   /*
    iterates through the all the flows in flow map removing expired flows and
    calling UpdateFlow if they are active.
   */
   void UpdateAllFlows();

   /*
    based on the calculate priority in the flow, puts the flow into respective level
    Args:
      f: Flow to put in the levels
   */
   void InsertFlow(Flow* f);

   /*
    obtain the next batch of packets from the highest priority level's Flows.
    Arg:
      A PacketBatch to insert the highest priority packets
   */
   void GetNextBatch(bess::PacketBatch *batch);

   /*
    dequeues from the specified slots from the flow f into the Packetbatch
    Args:
        batch: the batch to put the packets taken from the flow into
        f: the flow to take the packets from.
        slots: specifies the number of packets to get from the flow
    Returns:
        the number of packets dequeued from the flow.
   */
   int AddtoBatch(bess::PacketBatch *batch, Flow* f, int slots);

   /*
    allocates llring queue space and adds the queue to the specified flow with
    size indicated by slots.
    Args:
      f: Flow to add the queue to
      slots: the number of slots for the queue to have.
    Returns:
      0 on success and error value otherwise.
    */
   int AddQueue(Flow* f, int slots);

   uint8_t max_level_; // the highest priority in the queue
   float load_avg_; //moving load average over the number of flows
   int ready_flows_;// number of flows with the packets waiting to be sent
   uint32_t batch_size_;//the size of the output batch
   uint32_t max_queue_size_;//max size of a flow's queue before the module will start the flow's packets
   uint32_t init_flow_size_;//the initial size of a flow's queue
   std::map<Flow_Id, Flow> flows_;//state map used to reunite packets with their flow
   std::vector<std::vector<Flow*>> levels_;//used to determine a flow's priority

};

#endif  // BESS_MODULES_MLFQUEUE_H_
