#ifndef BESS_MODULES_FLOWGEN_H_
#define BESS_MODULES_FLOWGEN_H_

#include <deque>
#include <queue>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/random.h"

typedef std::pair<uint64_t, struct flow *> Event;
typedef std::priority_queue<Event, std::vector<Event>,
                            std::function<bool(Event, Event)>>
    EventQueue;

struct flow {
  int packets_left;
  int first;
  uint32_t next_seq_no;
  /* Note that these are in NETWORK ORDER */
  uint32_t src_ip, dst_ip;
  uint16_t src_port, dst_port;
};

class FlowGen final : public Module {
 public:
  enum Arrival {
    ARRIVAL_UNIFORM = 0,
    ARRIVAL_EXPONENTIAL,
  };

  enum Duration {
    DURATION_UNIFORM = 0,
    DURATION_PARETO,
  };

  static const gate_idx_t kNumIGates = 0;

  FlowGen()
      : Module(),
        active_flows_(),
        allocated_flows_(),
        generated_flows_(),
        flows_(),
        flows_free_(),
        events_(),
        templ_(),
        template_size_(),
        rng_(),
        arrival_(),
        duration_(),
        quick_rampup_(),
        total_pps_(),
        flow_rate_(),
        flow_duration_(),
        concurrent_flows_(),
        flow_pps_(),
        flow_pkts_(),
        flow_gap_ns_(),
        pareto_() {}

  pb_error_t Init(const bess::pb::FlowGenArg &arg);

  void DeInit() override;

  struct task_result RunTask(void *arg) override;

  std::string GetDesc() const override;

 private:
  inline double NewFlowPkts();
  inline double MaxFlowPkts() const;
  inline uint64_t NextFlowArrival();
  inline struct flow *ScheduleFlow(uint64_t time_ns);
  void MeasureParetoMean();
  void PopulateInitialFlows();
  
  pb_error_t UpdateBaseAddresses();
  bess::Packet *FillPacket(struct flow *f);
  void GeneratePackets(bess::PacketBatch *batch);

  pb_error_t InitFlowPool();
  pb_error_t ProcessArguments(const bess::pb::FlowGenArg &arg);

  int active_flows_;
  int allocated_flows_;
  uint64_t generated_flows_;
  struct flow *flows_;
  std::deque<struct flow *> flows_free_;

  EventQueue events_;

  char *templ_;
  int template_size_;

  Random rng_;

  Arrival arrival_;
  Duration duration_;

  /* behavior parameters */
  int quick_rampup_;

  /* load parameters */
  double total_pps_;
  double flow_rate_;     /* in flows/s */
  double flow_duration_; /* in seconds */

  /* derived variables */
  double concurrent_flows_; /* expected # of flows */
  double flow_pps_;         /* packets/s/flow */
  double flow_pkts_;        /* flow_pps * flow_duration */
  double flow_gap_ns_;      /* == 10^9 / flow_rate */

  /* ranges over which to vary ips and ports */
  uint32_t ip_src_range_;
  uint32_t ip_dst_range_;
  uint16_t port_src_range_;
  uint16_t port_dst_range_;

  /* base ip and ports IN HOST ORDER */
  uint32_t ip_src_base_;
  uint32_t ip_dst_base_;
  uint16_t port_src_base_;
  uint16_t port_dst_base_;



  struct {
    double alpha;
    double inversed_alpha; /* 1.0 / alpha */
    double mean;           /* determined by alpha */
  } pareto_;
};

#endif  // BESS_MODULES_FLOWGEN_H_
